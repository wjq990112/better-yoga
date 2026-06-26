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

## Initial findings (quick mode, better-yoga LTO+fast-path vs Taffy 0.7)

| scenario | better-yoga | Taffy 0.7 | winner |
|---|---|---|---|
| Wide tree /10000 | 3.11 ms | 3.78 ms | **yoga +18%** |
| Deep random /10000 | 2.97 ms | 4.32 ms | **yoga +31%** |
| Deep random /4000 | 1.11 ms | 1.43 ms | **yoga +22%** |
| Deep **auto-size** /10000 | 55.85 ms | 5.59 ms | **Taffy ~10x** |
| Deep **auto-size** /4000 | 17.14 ms | 2.03 ms | **Taffy ~8.4x** |

**The answer is workload-dependent**, not a clean win for either side:
better-yoga leads on wide/random trees, but Taffy crushes Yoga ~10x on deep
auto-size trees — a known Yoga algorithm weakness (deep nesting + auto sizing
triggers heavy redundant measurement). That gap is the next target for the
better-yoga refactor; it's exactly the "faster in *any* scenario" blind spot.

## Notes on fairness

- better-yoga built with its shipped config: `-O2 -flto=thin` + edge fast-path.
- Taffy built with cargo `release` defaults (opt-level 3, no LTO) — its standard
  shipped profile. Each side uses its own default build, which is what a user
  actually gets.
- To isolate *algorithm* from *build* differences, a second round can give Taffy
  `lto=true` in its release profile and re-measure.
