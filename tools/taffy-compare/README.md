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
| Deep auto-size /10000 | 40.3 ms | 5.31 ms | **Taffy 7.6x** |
| Super deep /50 | 1.72 ms | 80 µs | **Taffy 21x** |
| Super deep /100 | 7.21 ms | 163 µs | **Taffy 44x** |

**Updated with dynamic measurement cache** (commit 70c01426): inline 8 + heap
overflow instead of cyclic eviction. This fixed the exponential blow-up
(super-deep /100: 27.9s → 7.21ms, **3867x**) and made wide actually BEAT Taffy
(was parity: wide /10000 3.31ms→3.24ms vs Taffy 3.93ms, now **Yoga +21%**).

**The remaining gap is algorithmic**: deep auto-size (7-8 levels, fits in 8
inline cache slots so dynamic overflow doesn't help) still does ~7500 measure
calls vs Taffy's ~150. This is because Yoga's measure returns StretchFit(mainSize)
as the measurement result (each mainSize is a distinct cache key), while Taffy's
ComputeSize returns content (MaxContent, all mainSizes share one entry). Closing
this gap requires ComputeSize/PerformLayout separation — a major refactor.

## Notes on fairness

- better-yoga built with its shipped config: `-O2 -flto=thin` + edge fast-path.
- Taffy built with cargo `release` defaults (opt-level 3, no LTO) — its standard
  shipped profile. Each side uses its own default build, which is what a user
  actually gets.
- To isolate *algorithm* from *build* differences, a second round can give Taffy
  `lto=true` in its release profile and re-measure.
