# Taffy vs better-yoga comparison

A fair head-to-head of better-yoga against [Taffy](https://github.com/DioxusLabs/taffy),
the Rust layout engine used by Bevy/Dioxus/Zed. It reuses Taffy's own benches
(which already A/B a yoga binding against Taffy on the same random trees under
the same criterion timing) but repoints the `yoga` crate at a local patched copy
that builds **better-yoga** with its shipped config (LTO + edge fast-path).

## One-time setup

Requires Homebrew Rust: `brew install rust`.

```
tools/taffy-compare/setup.sh
```

This clones Taffy, fetches the crates.io `yoga` crate, replaces its vendored
upstream C++ with better-yoga's `yoga/` (fast-path included), patches the
build to add `-flto=thin` (reproducing better-yoga's Release default — verified
the emitted objects are LLVM bitcode so ThinLTO fires at link time), and
patches yoga-rs's Rust FFI to track better-yoga's newer C API. Three patches
under `patches/` capture every yoga-rs change.

## Run

```
tools/taffy-compare/run.sh                 # quick (~2 min)
FULL=1 tools/taffy-compare/run.sh          # full criterion samples
```

## Results (quick mode, better-yoga LTO+fast-path vs Taffy 0.7)

| scenario | better-yoga | Taffy 0.7 | verdict |
|---|---|---|---|
| Huge nested /10000 | 2.12 ms | 2.07 ms | ~parity |
| Wide tree /10000 | 3.31 ms | 3.38 ms | ~parity |
| Deep random /4000 | 1.12 ms | 1.39 ms | **yoga +20%** |
| Deep random /10000 | 3.10 ms | 4.47 ms | **yoga +31%** |
| Deep auto-size /4000 | 17.7 ms | 2.00 ms | **Taffy 8.8x** |
| Deep auto-size /10000 | 54.8 ms | 5.39 ms | **Taffy 10x** |
| Super deep /50 | 37.2 ms | 79.7 µs | **Taffy 466x** |
| Super deep /100 | 27.9 s | 180 µs | **Taffy ~155000x** |

**The answer is workload-dependent**, not a clean win for either side:
- On **wide / random-size** trees better-yoga leads (up to +31%) — its LTO + edge
  fast-path pay off.
- On **deep auto-size** trees Taffy is 8–10x faster, and on **super-deep** trees
  Yoga collapses catastrophically (27.9s for 100 nodes — exponential blow-up in
  deep nesting with auto sizing). This is a Yoga algorithm weakness, not
  something LTO/fast-path can touch, and it is the central "faster in *any*
  scenario" obstacle for the better-yoga refactor.

Note: `large` + `yoga-super-deep` together is why a full run takes ~40 min —
super-deep/100 alone is 27.9s × 10 samples. Default `run.sh` keeps super-deep
but drops `large`; set `FEATURES` to trim further for quick iteration.

## Notes on fairness

- better-yoga built with its shipped config: `-O2 -flto=thin` + edge fast-path.
- Taffy built with cargo `release` defaults (opt-level 3, no LTO) — its standard
  shipped profile. Each side uses its own default build, which is what a user
  actually gets.
- To isolate *algorithm* from *build* differences, a second round can give Taffy
  `lto=true` in its release profile and re-measure.
