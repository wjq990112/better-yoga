# better-yoga-layout [![npm](https://img.shields.io/npm/v/better-yoga-layout.svg)](https://www.npmjs.com/package/better-yoga-layout) [![license](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)

[Yoga](https://github.com/facebook/yoga) 的性能重构 fork —— 一个可嵌入、高性能、
带 WebAssembly 绑定的 flexbox 布局引擎。

`better-yoga-layout` 是 **`yoga-layout` 的 drop-in 替代品**：API 相同、布局结果
相同，但在真实 UI 树上更快。它在保持 Yoga 行为逐比特一致（完整的生成式一致性测试
全部通过）的前提下，替换了测量缓存并收紧了每节点热路径。

> 🌐 English docs: [README.md](./README.md)

## 安装

```sh
npm install better-yoga-layout
# 或
yarn add better-yoga-layout
```

## 使用

默认入口用 top-level await 实例化 WebAssembly 模块，`import` 进来即可直接使用：

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

如果你的运行环境不支持 top-level await，请改用 `/load` 入口，手动实例化 Yoga：

```ts
import {loadYoga, Align} from 'better-yoga-layout/load';

const Yoga = await loadYoga();
const node = Yoga.Node.create();
node.setAlignContent(Align.Center);
```

因为公共 API 与 `yoga-layout` 完全一致，迁移通常只是换一个依赖：

```diff
- import Yoga from 'yoga-layout';
+ import Yoga from 'better-yoga-layout';
```

## 改动了什么

所有改动都与 upstream Yoga **行为一致**——由完整的生成式一致性测试，以及一个逐树
对比本 fork 与 upstream 基线的 differential 布局工具共同验证。性能提升来自"用更少
的开销做同样的事"，外加一处对测量缓存的算法改动。

### 1. 动态测量缓存（Dynamic measurement cache）

upstream Yoga 把节点的测量结果缓存在**固定 8 槽、循环淘汰（cyclic eviction）**的
数组里。当一个节点在很多不同约束下被测量时（深层 auto-sized 链），这个 8 槽缓存会
反复抖动：条目被淘汰又被重算，最坏情况退化成病态级别的重复测量。

better-yoga 用**动态缓存——8 个 inline 槽 + 堆上溢出（heap overflow）**取代它，让
热点约束常驻而不被淘汰。常见情况依然零分配，同时消除了抖动。

- 深层 auto-sized 树提速约 8.6x。
- 把 upstream 在病态深链上的耗时从**秒级降到毫秒级**（见 [Benchmark](#benchmark)）。
- 提交 [`70c01426`](https://github.com/wjq990112/better-yoga/commit/70c01426)

### 2. 每节点热路径上的 Edge / 轴向 fast-path

margin、border、padding 的解析对每个节点、每一趟布局都会执行。better-yoga 对常见
情况做了短路：

- 当某个 edge 组为空时，完全跳过逐边解析
  （[`586bca7d`](https://github.com/wjq990112/better-yoga/commit/586bca7d)）。
- `computeMarginForAxis` / `computeBorderForAxis` 的轴向 fast-path
  （[`c5acf17b`](https://github.com/wjq990112/better-yoga/commit/c5acf17b)）。
- padding 的逐边 fast-path
  （[`b875c767`](https://github.com/wjq990112/better-yoga/commit/b875c767)）。
- 把显式的 `Point(0)` margin/padding/border 视为未设置，让它也走 fast-path
  （[`86dfc325`](https://github.com/wjq990112/better-yoga/commit/86dfc325)）。

### 3. 默认开启 ThinLTO

Release 构建启用 `-flto=thin`，让优化器跨布局引擎的多个编译单元做内联
（[`5a6fbf86`](https://github.com/wjq990112/better-yoga/commit/5a6fbf86)）。

## Benchmark

**在典型 UI 树上（huge-nested、wide、deep），better-yoga 比 upstream Yoga 快
1.5–1.9x**，并消除了 upstream 在病态深链上的灾难性退化。

下面的数据来自 [Taffy](https://github.com/DioxusLabs/taffy) 自带的 benchmark 套件
——它本身就在同样随机生成的树上、用同样的 Criterion 计时，A/B 对比一个 Yoga 绑定
和 Taffy。我们把它跑两遍——一遍对 upstream Yoga 的 vendored C++，一遍对
better-yoga——再合并成一张表。为了把**算法**和**构建**区分开，**两个 Yoga 变体都用
`-O2 -flto=thin` 构建**；Taffy 用它的 `release` 默认配置。

| 场景 | upstream Yoga | better-yoga | Taffy 0.7 | better-yoga vs upstream |
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

super-deep 那几个夸张的数字**不是**一个普适的提速结论——它们反映的是 upstream Yoga
的循环淘汰缓存在极端深链上**退化**（super-deep/100：upstream **35 秒** vs 动态缓存
**6.8 ms**）。如果你的树会命中这种形状，动态缓存就是"卡死"和"瞬间完成"的区别；但要
注意，在这些相同形状上 **Taffy 仍然远快于**两个 Yoga（见下面的坦诚说明）。

复现方式见 [`tools/taffy-compare`](./tools/taffy-compare/README.md)。

### 坦诚的局限

better-yoga 在所有场景都比 upstream Yoga 快，但它**并没有**追平 Taffy 的每一处差距：

- 在 **deep auto-sized** 和 **super-deep** 树上，Taffy 仍然明显更快（大约分别快 7x
  和 20–40x）。剩下的差距是**算法层面的**：Yoga 的 measure 返回
  `StretchFit(mainSize)`——每个 main size 都是一个不同的缓存 key；而 Taffy 的
  `ComputeSize` 返回内容尺寸（`MaxContent`），所有 main size 共享同一个缓存条目。
  要追平这一点需要做 `ComputeSize` / `PerformLayout` 的拆分，是一次大型重构，本
  fork 未做。
- 在 huge-nested、wide、deep-random 树上，better-yoga *领先于* Taffy。

一句话：如果你已经在用 `yoga-layout`，better-yoga 是行为一致的免费提速；如果你在为
新项目挑选引擎、且负载以深层 auto-sizing 为主，建议同时评估 Taffy。

## 致谢

本项目是 Meta Platforms, Inc. 的 [Yoga](https://github.com/facebook/yoga) 的
fork，遵循 MIT 许可证。所有 flexbox 语义、一致性 fixtures 和核心算法都是 upstream
Yoga 的工作；本 fork 仅在其之上添加了上述性能改动。详见 [LICENSE](./LICENSE)。

---

## 参与贡献

### 构建

Yoga 的主实现面向 C++ 20，配套 CMake 构建逻辑。提供了一个脚本来构建主库并运行单元
测试：

```sh
./unit_tests <Debug|Release>
```

虽非必需，安装了 [ninja](https://ninja-build.org/) 时该脚本会用它来加速构建。

JavaScript / WebAssembly 包在 `javascript` 目录下构建：

```sh
cd javascript
yarn install
yarn build   # 通过 Emscripten 编译 WASM 产物
yarn test    # 运行一致性 + 单元测试套件
```

### 添加测试

Yoga 的许多测试是自动生成的，使用描述节点结构的 HTML fixtures。这些 fixture 会在
Chrome 中渲染，生成树的期望布局结果。新 fixture 可加到 `gentest/fixtures`：

```html
<div id="my_test" style="width: 100px; height: 100px; align-items: center;">
  <div style="width: 50px; height: 50px;"></div>
</div>
```

从新增 fixture 生成测试：

1. 确保已安装 [yarn classic](https://classic.yarnpkg.com)。
2. 运行 `yarn install` 安装测试生成器的依赖。
3. 在 `yoga` 目录运行 `yarn gentest`。

### 调试

Yoga 提供了 VSCode `launch.json` 配置用于调试单元测试。加好断点后，运行
"Debug C++ Unit tests (lldb)"（Windows 上为 "Debug C++ Unit tests (vsdbg)"）。
