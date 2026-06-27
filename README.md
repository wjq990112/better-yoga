# better-yoga-layout [![npm](https://img.shields.io/npm/v/better-yoga-layout.svg)](https://www.npmjs.com/package/better-yoga-layout) [![license](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)

A performance-refactored fork of [Yoga](https://github.com/facebook/yoga) — an
embeddable, high-performance flexbox layout engine with WebAssembly bindings.

`better-yoga-layout` is a **drop-in replacement for `yoga-layout`**: same API,
same layout results, faster on real-world UI trees. It keeps Yoga's behavior
bit-for-bit (the full generated conformance suite passes) while replacing the
measurement cache and tightening the per-node hot path.

> 🇨🇳 中文文档见 [README.zh-CN.md](./README.zh-CN.md)

## Install

```sh
npm install better-yoga-layout
# or
yarn add better-yoga-layout
```

## Usage

The default entry point uses top-level await to instantiate the WebAssembly
module, so `import` gives you a ready-to-use engine:

```ts
import Yoga, {Direction, Edge, FlexDirection} from 'better-yoga-layout';

const root = Yoga.Node.create();
root.setFlexDirection(FlexDirection.Row);
root.setWidth(200);
root.setHeight(100);

const child = Yoga.Node.create();
child.setFlexGrow(1);
child.setMargin(Edge.All, 10);
root.insertChild(child, 0);

root.calculateLayout(undefined, undefined, Direction.LTR);
console.log(root.getComputedLayout()); // { left, top, width, height, ... }
```

If your environment does not support top-level await, use the `/load` entry
point and instantiate Yoga manually:

```ts
import {loadYoga, Align} from 'better-yoga-layout/load';

const Yoga = await loadYoga();
const node = Yoga.Node.create();
node.setAlignContent(Align.Center);
```

Because the public API is identical to `yoga-layout`, migrating is usually just
a dependency swap:

```diff
- import Yoga from 'yoga-layout';
+ import Yoga from 'better-yoga-layout';
```

## What's different

All changes are **behavior-identical** to upstream Yoga — verified by the full
generated conformance suite and a differential layout harness that diffs this
fork against the upstream baseline tree-by-tree. The wins come from doing the
same work with less overhead, plus one algorithmic change to the measurement
cache.

### 1. Dynamic measurement cache

Upstream Yoga caches a node's measurement results in a **fixed array of 8 slots
with cyclic eviction**. On trees where a node is measured under many distinct
constraints (deep auto-sized chains), the 8-slot cache thrashes: entries are
evicted and recomputed repeatedly, which can degenerate into pathological
re-measurement.

better-yoga replaces it with a **dynamic cache — 8 inline slots plus heap
overflow** — so hot constraints stay resident instead of being evicted. This
keeps the common case allocation-free while eliminating the thrash.

- ~8.6x faster on deep auto-sized trees.
- Turns upstream's pathological deep chains from **seconds into milliseconds**
  (see [Benchmarks](#benchmarks)).
- Commit [`70c01426`](https://github.com/wjq990112/better-yoga/commit/70c01426)

### 2. Edge / axis fast-paths on the per-node hot path

Margin, border, and padding resolution runs for every node, every layout pass.
better-yoga short-circuits the common cases:

- Skip per-edge resolution entirely when an edge group is empty
  ([`586bca7d`](https://github.com/wjq990112/better-yoga/commit/586bca7d)).
- Axis-level fast-path for `computeMarginForAxis` / `computeBorderForAxis`
  ([`c5acf17b`](https://github.com/wjq990112/better-yoga/commit/c5acf17b)).
- Per-edge fast-path for padding
  ([`b875c767`](https://github.com/wjq990112/better-yoga/commit/b875c767)).
- Treat an explicit `Point(0)` margin/padding/border as unset, so it takes the
  fast path too
  ([`86dfc325`](https://github.com/wjq990112/better-yoga/commit/86dfc325)).

### 3. ThinLTO by default

Release builds enable `-flto=thin`, letting the optimizer inline across the
layout engine's translation units
([`5a6fbf86`](https://github.com/wjq990112/better-yoga/commit/5a6fbf86)).

## Benchmarks

**better-yoga is 1.5–1.9x faster than upstream Yoga on typical UI trees**
(huge-nested, wide, and deep trees), and removes upstream's catastrophic
slowdown on pathological deep chains.

The numbers below come from [Taffy](https://github.com/DioxusLabs/taffy)'s own
benchmark suite, which already A/B's a Yoga binding against Taffy on the same
randomly generated trees under the same Criterion timing. We run it twice —
once against upstream Yoga's vendored C++, once against better-yoga — and merge
the results into one table. To isolate *algorithm* from *build*, **both Yoga
variants are built `-O2 -flto=thin`**; Taffy uses its `release` defaults.

| Scenario | upstream Yoga | better-yoga | Taffy 0.7 | better-yoga vs upstream |
|---|---|---|---|---|
| Huge nested /1000 | 263.5 µs | 162.0 µs | 199.7 µs | **1.6x** |
| Huge nested /10000 | 2.60 ms | 1.76 ms | 2.12 ms | **1.5x** |
| Wide tree /1000 | 354.1 µs | 221.5 µs | 306.6 µs | **1.6x** |
| Wide tree /10000 | 3.78 ms | 2.38 ms | 3.31 ms | **1.6x** |
| Deep tree (random size) /4000 | 1.35 ms | 926.4 µs | 1.41 ms | **1.5x** |
| Deep tree (random size) /10000 | 3.38 ms | 2.21 ms | 4.19 ms | **1.5x** |
| Deep tree (auto size) /4000 | 22.93 ms | 12.84 ms | 1.93 ms | **1.8x** |
| Deep tree (auto size) /10000 | 72.83 ms | 37.35 ms | 5.45 ms | **1.9x** |
| Super deep /50 | 44.71 ms | 1.62 ms | 82.0 µs | **27.6x** |
| Super deep /100 | 35.45 s | 6.78 ms | 163.7 µs | **5227x** |

The huge **super-deep** numbers are not a general speedup claim — they reflect
upstream Yoga's cyclic-eviction cache *degenerating* on extreme deep chains
(super-deep/100 takes **35 seconds** upstream vs **6.8 ms** with the dynamic
cache). If your trees ever hit that shape, the dynamic cache is the difference
between hanging and instant; but note that on these same shapes **Taffy is
still far faster** than either Yoga (see caveats).

To reproduce, see [`tools/taffy-compare`](./tools/taffy-compare/README.md).

### Honest caveats

better-yoga is faster than upstream Yoga everywhere, but it does **not** close
every gap against Taffy:

- On **deep auto-sized** and **super-deep** trees, Taffy is still
  significantly faster (roughly 7x and 20–40x respectively). The remaining gap
  is **algorithmic**: Yoga's measure returns `StretchFit(mainSize)` — each main
  size is a distinct cache key — while Taffy's `ComputeSize` returns content
  (`MaxContent`), so all main sizes share one cache entry. Closing this needs a
  `ComputeSize` / `PerformLayout` separation, a major refactor not done here.
- On huge-nested, wide, and deep-random trees, better-yoga is *ahead of* Taffy.

In short: if you're already using `yoga-layout`, better-yoga is a free speedup
with identical behavior. If you're choosing a brand-new engine and your
workload is dominated by deep auto-sizing, evaluate Taffy too.

## Credits

This project is a fork of [Yoga](https://github.com/facebook/yoga) by Meta
Platforms, Inc., distributed under the MIT license. All flexbox semantics,
conformance fixtures, and the core algorithm are upstream Yoga's work; this
fork adds the performance changes described above. See [LICENSE](./LICENSE).

---

## Contributing

### Building

Yoga's main implementation targets C++ 20 with accompanying build logic in
CMake. A wrapper is provided to build the main library and run unit tests:

```sh
./unit_tests <Debug|Release>
```

While not required, this script will use [ninja](https://ninja-build.org/) if it
is installed, for faster builds.

For the JavaScript/WebAssembly package, build from the `javascript` directory:

```sh
cd javascript
yarn install
yarn build   # compiles the WASM binary via Emscripten
yarn test    # runs the conformance + unit suites
```

### Adding tests

Many of Yoga's tests are automatically generated, using HTML fixtures
describing node structure. These are rendered in Chrome to generate an expected
layout result for the tree. New fixtures can be added to `gentest/fixtures`:

```html
<div id="my_test" style="width: 100px; height: 100px; align-items: center;">
  <div style="width: 50px; height: 50px;"></div>
</div>
```

To generate new tests from added fixtures:

1. Ensure you have [yarn classic](https://classic.yarnpkg.com) installed.
2. Run `yarn install` to install dependencies for the test generator.
3. Run `yarn gentest` in the `yoga` directory.

### Debugging

Yoga provides a VSCode `launch.json` configuration which allows debugging unit
tests. Add your breakpoints, and run "Debug C++ Unit tests (lldb)" (or "Debug
C++ Unit tests (vsdbg)" on Windows).
