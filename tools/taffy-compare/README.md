# Upstream Yoga vs better-yoga vs Taffy

A fair three-way head-to-head: **upstream Yoga** (the crates.io `yoga` crate's
vendored C++ — no fast-path, no dynamic cache) vs **better-yoga** (this fork:
LTO + edge/axis/padding fast-paths + dynamic measurement cache) vs **Taffy 0.7**
(the Rust layout engine used by Bevy/Dioxus/Zed). It reuses Taffy's own benches
(which already A/B a yoga binding against Taffy on the same random trees under
the same criterion timing) and runs them twice — once pointing the `yoga` crate
at a local copy of upstream Yoga, once at better-yoga — then merges the two
runs into a three-column table.

## One-time setup

Requires Homebrew Rust: `brew install rust`.

```
tools/taffy-compare/setup.sh
```

This clones Taffy, fetches the crates.io `yoga` crate, and prepares TWO local
copies of it:

- `yoga-rs-original` — keeps the vendored upstream C++ (no better-yoga changes);
  build patched to add `-flto=thin` so the upstream-vs-better-yoga comparison
  isolates *algorithm* from *build* differences. Upstream C++ matches yoga-rs's
  original FFI, so no FFI patches are applied.
- `yoga-rs-patched` — vendored C++ replaced with better-yoga's `yoga/`
  (fast-paths + dynamic cache included), plus the same `-flto=thin` and FFI
  patches tracking better-yoga's newer C API.

Three patches under `patches/` capture the yoga-rs changes (LTO build flag,
`YGNodeStyleGetGap` → `YGValue`, `YGUnit` sizing keywords).

## Run

```
tools/taffy-compare/run-three-way.sh        # three-way (~15 min; upstream super-deep/100 is 35 s)
tools/taffy-compare/run.sh                  # two-way: better-yoga vs Taffy only (~2 min)
FULL=1 tools/taffy-compare/run.sh           # full criterion samples
```

`run-three-way.sh` rewrites the `[patch.crates-io] yoga = …` line in Taffy's
`benches/Cargo.toml` to point at each variant in turn, runs criterion under a
named baseline (`upstream`, then `better-yoga`), and merges the two baselines
into a three-column table. It deliberately omits the `large` feature
(huge-nested/100000, super-deep/200): upstream Yoga's cyclic cache eviction
explodes there — super-deep/100 alone is 35 s — so /200 would take many minutes
per sample for no extra insight.

## Results (quick mode; upstream & better-yoga both -O2 -flto=thin; Taffy release default)

| scenario | upstream yoga | better-yoga | Taffy 0.7 | better-yoga vs upstream |
|---|---|---|---|---|
| Huge nested /1000 | 263.5 µs | 162.0 µs | 199.7 µs | 1.6x faster |
| Huge nested /10000 | 2.60 ms | 1.76 ms | 2.12 ms | 1.5x faster |
| Wide tree /1000 | 354.1 µs | 221.5 µs | 306.6 µs | 1.6x faster |
| Wide tree /10000 | 3.78 ms | 2.38 ms | 3.31 ms | 1.6x faster |
| Deep tree (random size) /4000 | 1.35 ms | 926.4 µs | 1.41 ms | 1.5x faster |
| Deep tree (random size) /10000 | 3.38 ms | 2.21 ms | 4.19 ms | 1.5x faster |
| Deep tree (auto size) /4000 | 22.93 ms | 12.84 ms | 1.93 ms | 1.8x faster |
| Deep tree (auto size) /10000 | 72.83 ms | 37.35 ms | 5.45 ms | 1.9x faster |
| Super deep /50 | 44.71 ms | 1.62 ms | 82.0 µs | 27.6x faster |
| Super deep /100 | 35.45 s | 6.78 ms | 163.7 µs | **5227x faster** |

Taffy 0.7 is identical code in both runs; the table shows the min of the two.
Cross-run consistency is within ~6% except Wide tree /10000 (~17%, a
`sample_size=10` / P/E-core scheduling outlier).

### What this shows

- **better-yoga vs upstream Yoga** — faster in *every* scenario: 1.5–1.9x on
  typical trees, and **27.6x / 5227x on super-deep** thanks to the dynamic
  measurement cache (commit `70c01426`), which replaces upstream's cyclic
  eviction. Super-deep/100 goes from **35 seconds to 6.8 ms**.
- **better-yoga vs Taffy** — better-yoga wins on huge-nested, wide, and
  deep-random trees (+17–47%); Taffy wins on deep auto-size (~7x) and
  catastrophically on super-deep (~20–41x).
- **The remaining gap is algorithmic.** Deep auto-size and super-deep still do
  thousands of measure calls vs Taffy's ~150, because Yoga's measure returns
  `StretchFit(mainSize)` as the result (each mainSize is a distinct cache key)
  while Taffy's `ComputeSize` returns content (`MaxContent`, all mainSizes share
  one entry). Closing this needs `ComputeSize`/`PerformLayout` separation — a
  major refactor.

### History note: why earlier tables had no upstream column

An earlier version of this harness had a bug: the `[patch.crates-io]` line
pointed at `yoga-rs-original` (upstream Yoga) instead of `yoga-rs-patched`
(better-yoga). So the *first* "Yoga" columns published were actually upstream
Yoga — including super-deep/100 exploding to ~46 s. Once the patch was
repointed at better-yoga, the upstream column disappeared. This three-way run
restores it explicitly, so upstream Yoga, better-yoga, and Taffy are all visible
side by side.

## Notes on fairness

- **upstream Yoga and better-yoga both build with `-O2 -flto=thin`** (setup.sh
  patches build.rs). This isolates *algorithm* from *build* differences: the
  upstream-vs-better-yoga column is purely the fast-path + dynamic-cache work.
  better-yoga's shipped config is also `-O2` + ThinLTO, so this is its real
  build; upstream Yoga's shipped config is `-O3` without LTO, so we give it LTO
  here to avoid attributing build-layer gains to the algorithm.
- **Taffy** is built with cargo `release` defaults (opt-level 3, no LTO) — its
  standard shipped profile. Each side uses its own default build, which is what
  a user actually gets.
- To isolate *Taffy's* algorithm from its build, a second round can give Taffy
  `lto=true` in its release profile and re-measure.
