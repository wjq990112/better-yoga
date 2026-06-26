# better-yoga-layout

Prebuilt WebAssembly bindings for **better-yoga** — a performance-refactored
fork of [Yoga](https://github.com/facebook/yoga).

`better-yoga-layout` is a **drop-in replacement for `yoga-layout`**: identical
API and layout results, faster on real-world UI trees (1.5–1.9x vs upstream
Yoga on typical trees). It swaps Yoga's fixed-size measurement cache for a
dynamic one and adds edge/axis fast-paths on the per-node hot path, while
keeping behavior bit-for-bit identical.

See the [project README](https://github.com/wjq990112/better-yoga#readme) for
the full list of optimizations and benchmarks.

## Usage

```ts
import Yoga, {Align} from 'better-yoga-layout';

const node = Yoga.Node.create();
node.setAlignContent(Align.Center);
```

Migrating from `yoga-layout` is usually just a dependency swap:

```diff
- import Yoga from 'yoga-layout';
+ import Yoga from 'better-yoga-layout';
```

## Requirements

`better-yoga-layout` requires a toolchain that supports ES Modules and
top-level await.

If top-level await is not supported, use the `better-yoga-layout/load` entry
point instead. This requires loading yoga manually:

```ts
import {loadYoga, Align} from 'better-yoga-layout/load';

const node = (await loadYoga()).Node.create();
node.setAlignContent(Align.Center);
```

## License

MIT. A fork of [Yoga](https://github.com/facebook/yoga) by Meta Platforms, Inc.
