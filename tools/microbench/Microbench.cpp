/*
 * better-yoga layout microbenchmark.
 *
 * Unlike benchmark/Benchmark.cpp (which rebuilds a JSON-deserialized tree every
 * repetition, mixing nlohmann-dominated tree construction into the timing and
 * leaving layout's small absolute time buried under run-to-run noise), this
 * tool builds each tree *once* via the stable public C API and times only the
 * layout phase, in two modes:
 *
 *   cold     — mark the whole tree dirty, then YGNodeCalculateLayout. Models a
 *              first layout / full invalidation. No measurement cache reuse.
 *   relayout — mark a single leaf dirty (propagating up), then re-layout. Models
 *              a React-style re-render where most of the tree is cache-clean.
 *
 * Because it links the public C API only (unchanged since the fork point), the
 * exact same source compiles against either yogacore, so it doubles as an A/B
 * harness. Workloads are constructed programmatically to exercise specific
 * layout features (aspect-ratio, wrap, percentages, absolute, RTL, deep
 * nesting, min/max) that the recorded captures barely cover.
 *
 * Each (workload, mode) pair runs many iterations; we report the *minimum*
 * nanosecond time — layout is deterministic, so the true cost is the infimum
 * and scheduler/thermal noise only inflates observed times.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <yoga/Yoga.h>

using namespace std::chrono;

namespace {

// A measure function mimicking text/leaf content: return a size derived from
// the available width, with no sleep (so timing reflects pure algorithm cost).
YGSize measureLeaf(
    YGNodeConstRef /*node*/,
    float availableWidth,
    YGMeasureMode widthMode,
    float /*availableHeight*/,
    YGMeasureMode /*heightMode*/) {
  float w = (widthMode == YGMeasureModeUndefined) ? 50.0f
                                                  : std::min(availableWidth, 50.0f);
  // Pretend content wraps to a height proportional to how narrow it is.
  float h = w > 0 ? (2500.0f / w) : 20.0f;
  return YGSize{w, h};
}

struct Workload {
  std::string name;
  YGNodeRef root;
  float availableWidth;
  float availableHeight;
  YGDirection direction;
  std::vector<YGNodeRef> leaves; // for relayout dirtying
};

YGConfigRef makeConfig() {
  YGConfigRef config = YGConfigNew();
  YGConfigSetPointScaleFactor(config, 2.0f);
  return config;
}

// Default config (pointScaleFactor=0) — mirrors yoga-rs's Node::new(). Pixel-grid
// rounding is OFF, so cache keys compare exactly: distinct mainSizes per depth
// level never collapse, which can blow up deep auto-size trees.
YGConfigRef makeConfigDefault() {
  return YGConfigNew();
}

// Collect leaf nodes (no children) for relayout dirtying.
void collectLeaves(YGNodeRef node, std::vector<YGNodeRef>& out) {
  size_t count = YGNodeGetChildCount(node);
  if (count == 0) {
    out.push_back(node);
    return;
  }
  for (size_t i = 0; i < count; i++) {
    collectLeaves(YGNodeGetChild(node, i), out);
  }
}

// ---- Workload builders -----------------------------------------------------

// Wide-and-deep flex tree: `fanout` children per node, `depth` levels, rows and
// columns alternating. Mix of fixed, flex-grow, and flex-shrink children.
YGNodeRef buildFlexTree(
    YGConfigRef config,
    int depth,
    int fanout,
    bool row) {
  YGNodeRef node = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(
      node, row ? YGFlexDirectionRow : YGFlexDirectionColumn);
  if (depth == 0) {
    YGNodeStyleSetWidth(node, 40.0f);
    YGNodeStyleSetHeight(node, 20.0f);
    return node;
  }
  for (int i = 0; i < fanout; i++) {
    YGNodeRef child = buildFlexTree(config, depth - 1, fanout, !row);
    // Vary flex behavior across children.
    if (i % 3 == 0) {
      YGNodeStyleSetFlexGrow(child, 1.0f);
    } else if (i % 3 == 1) {
      YGNodeStyleSetFlexShrink(child, 1.0f);
      YGNodeStyleSetFlexBasis(child, 100.0f);
    }
    YGNodeStyleSetMargin(child, YGEdgeAll, 2.0f);
    YGNodeInsertChild(node, child, (size_t)i);
  }
  return node;
}

// Aspect-ratio heavy: a grid of image-like nodes with aspect ratios and stretch.
YGNodeRef buildAspectRatioGrid(YGConfigRef config, int rows, int cols) {
  YGNodeRef root = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
  for (int r = 0; r < rows; r++) {
    YGNodeRef rowNode = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexDirection(rowNode, YGFlexDirectionRow);
    YGNodeStyleSetAlignItems(rowNode, YGAlignStretch);
    for (int c = 0; c < cols; c++) {
      YGNodeRef cell = YGNodeNewWithConfig(config);
      YGNodeStyleSetFlexGrow(cell, 1.0f);
      YGNodeStyleSetAspectRatio(cell, 1.5f + (float)((r + c) % 3) * 0.25f);
      YGNodeStyleSetMargin(cell, YGEdgeAll, 1.0f);
      YGNodeInsertChild(rowNode, cell, (size_t)c);
    }
    YGNodeInsertChild(root, rowNode, (size_t)r);
  }
  return root;
}

// Flex-wrap with many items that overflow onto multiple lines.
YGNodeRef buildWrap(YGConfigRef config, int items) {
  YGNodeRef root = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(root, YGFlexDirectionRow);
  YGNodeStyleSetFlexWrap(root, YGWrapWrap);
  YGNodeStyleSetAlignContent(root, YGAlignFlexStart);
  YGNodeStyleSetGap(root, YGGutterAll, 4.0f);
  for (int i = 0; i < items; i++) {
    YGNodeRef item = YGNodeNewWithConfig(config);
    YGNodeStyleSetWidth(item, 60.0f + (float)(i % 5) * 10.0f);
    YGNodeStyleSetHeight(item, 30.0f);
    YGNodeInsertChild(root, item, (size_t)i);
  }
  return root;
}

// Wide shallow tree (Taffy's "Wide tree" shape): a root with many children,
// each a leaf with fixed size. This is the workload cache=64 was suspected of
// regressing (memory pressure). Used to verify whether the regression is real.
YGNodeRef buildWide(YGConfigRef config, int items) {
  YGNodeRef root = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(root, YGFlexDirectionRow);
  YGNodeStyleSetFlexWrap(root, YGWrapWrap);
  for (int i = 0; i < items; i++) {
    YGNodeRef item = YGNodeNewWithConfig(config);
    YGNodeStyleSetWidth(item, 80.0f);
    YGNodeStyleSetHeight(item, 40.0f);
    YGNodeStyleSetMargin(item, YGEdgeAll, 2.0f);
    YGNodeInsertChild(root, item, (size_t)i);
  }
  return root;
}

// Taffy "huge nested" shape: a deep hierarchy of fixed-size (10pt) flex-grow:1
// nodes with a fixed branching factor, laid out under a definite viewport.
// Mirrors taffy-compare's huge_nested benchmark (build_deep_hierarchy).
YGNodeRef buildHugeNested(YGConfigRef config, uint32_t nodeCount, uint32_t branching) {
  std::function<YGNodeRef(uint32_t)> build = [&](uint32_t n) -> YGNodeRef {
    YGNodeRef node = YGNodeNewWithConfig(config);
    YGNodeStyleSetWidth(node, 10.0f);
    YGNodeStyleSetHeight(node, 10.0f);
    YGNodeStyleSetFlexGrow(node, 1.0f);
    if (n <= branching) {
      for (uint32_t i = 0; i < n; i++) {
        YGNodeRef leaf = YGNodeNewWithConfig(config);
        YGNodeStyleSetWidth(leaf, 10.0f);
        YGNodeStyleSetHeight(leaf, 10.0f);
        YGNodeStyleSetFlexGrow(leaf, 1.0f);
        YGNodeInsertChild(node, leaf, i);
      }
      return node;
    }
    uint32_t childNodes = (n - branching) / branching;
    for (uint32_t i = 0; i < branching; i++) {
      YGNodeInsertChild(node, build(childNodes), i);
    }
    return node;
  };
  return build(nodeCount);
}

// Percentage-sized nested containers with min/max constraints.
YGNodeRef buildPercentMinMax(YGConfigRef config, int depth) {
  YGNodeRef node = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(
      node, depth % 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
  YGNodeStyleSetWidthPercent(node, 100.0f);
  YGNodeStyleSetHeightPercent(node, 100.0f);
  YGNodeStyleSetPadding(node, YGEdgeAll, 4.0f);
  if (depth == 0) {
    return node;
  }
  for (int i = 0; i < 3; i++) {
    YGNodeRef child = buildPercentMinMax(config, depth - 1);
    YGNodeStyleSetFlexGrow(child, 1.0f);
    YGNodeStyleSetMinWidthPercent(child, 10.0f);
    YGNodeStyleSetMaxWidthPercent(child, 90.0f);
    YGNodeStyleSetMinHeight(child, 20.0f);
    YGNodeInsertChild(node, child, (size_t)i);
  }
  return node;
}

// Tree with measure-function leaves (text-like), exercising the measure cache.
YGNodeRef buildTextHeavy(YGConfigRef config, int rows, std::vector<YGNodeRef>& leaves) {
  YGNodeRef root = YGNodeNewWithConfig(config);
  YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
  for (int r = 0; r < rows; r++) {
    YGNodeRef line = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexDirection(line, YGFlexDirectionRow);
    YGNodeStyleSetAlignItems(line, YGAlignFlexStart);
    for (int c = 0; c < 4; c++) {
      YGNodeRef text = YGNodeNewWithConfig(config);
      YGNodeStyleSetFlexShrink(text, 1.0f);
      YGNodeSetMeasureFunc(text, measureLeaf);
      YGNodeStyleSetMargin(text, YGEdgeAll, 2.0f);
      YGNodeInsertChild(line, text, (size_t)c);
      leaves.push_back(text);
    }
    YGNodeInsertChild(root, line, (size_t)r);
  }
  return root;
}

// Absolutely-positioned descendants over a relative container.
YGNodeRef buildAbsolute(YGConfigRef config, int items) {
  YGNodeRef root = YGNodeNewWithConfig(config);
  YGNodeStyleSetWidth(root, 400.0f);
  YGNodeStyleSetHeight(root, 400.0f);
  for (int i = 0; i < items; i++) {
    YGNodeRef item = YGNodeNewWithConfig(config);
    YGNodeStyleSetPositionType(item, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(item, YGEdgeLeft, (float)(i % 20) * 18.0f);
    YGNodeStyleSetPosition(item, YGEdgeTop, (float)(i / 20) * 18.0f);
    YGNodeStyleSetWidth(item, 16.0f);
    YGNodeStyleSetHeight(item, 16.0f);
    YGNodeInsertChild(root, item, (size_t)i);
  }
  return root;
}

// Super-deep auto-size chain mirroring Taffy's super_deep benchmark: a `depth`
// chain of containers, each holding the previous level's node plus two leaves.
// Every node is Row + flexGrow:1 + margin:10, sized auto. This is the workload
// where Yoga collapses exponentially (27.9s for depth 100) and Taffy wins ~10x.
YGNodeRef buildSuperDeep(YGConfigRef config, int depth, int nodesPerLevel) {
  // Build bottom-up: children starts empty, each level wraps it in a container
  // and adds (nodesPerLevel - 1) leaves.
  std::vector<YGNodeRef> children;
  for (int d = 0; d < depth; d++) {
    YGNodeRef container = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexDirection(container, YGFlexDirectionRow);
    YGNodeStyleSetFlexGrow(container, 1.0f);
    YGNodeStyleSetMargin(container, YGEdgeAll, 10.0f);
    for (size_t i = 0; i < children.size(); i++) {
      YGNodeInsertChild(container, children[i], i);
    }
    children.clear();
    children.push_back(container);
    for (int i = 1; i < nodesPerLevel; i++) {
      YGNodeRef leaf = YGNodeNewWithConfig(config);
      YGNodeStyleSetFlexDirection(leaf, YGFlexDirectionRow);
      YGNodeStyleSetFlexGrow(leaf, 1.0f);
      YGNodeStyleSetMargin(leaf, YGEdgeAll, 10.0f);
      children.push_back(leaf);
    }
  }
  // The last "children" holds the outermost level; promote its first node as root.
  YGNodeRef root = children[0];
  return root;
}

// Apply the same style fields taffy-compare's yoga binding sets on every node
// (apply_taffy_style, using Taffy's defaults) — to isolate which field triggers
// the 78x slowdown seen in taffy-compare vs the bare buildSuperDeep path.
void applyTaffyIshStyle(YGNodeRef node) {
  // Precisely mirrors apply_taffy_style over Taffy's Style::DEFAULT, which is
  // what taffy-compare feeds every node. Key vs bare Yoga defaults: min/max
  // stay AUTO (undefined), padding/border are explicitly Length(0) (sets the
  // ever-set sticky bits), align_items is Auto (not Yoga's default Stretch).
  YGNodeStyleSetDisplay(node, YGDisplayFlex);
  YGNodeStyleSetBoxSizing(node, YGBoxSizingBorderBox);
  YGNodeStyleSetPositionType(node, YGPositionTypeRelative);
  YGNodeStyleSetPositionAuto(node, YGEdgeLeft);
  YGNodeStyleSetPositionAuto(node, YGEdgeRight);
  YGNodeStyleSetPositionAuto(node, YGEdgeTop);
  YGNodeStyleSetPositionAuto(node, YGEdgeBottom);
  YGNodeStyleSetWidthAuto(node);
  YGNodeStyleSetHeightAuto(node);
  // min/max stay AUTO (undefined) — do NOT set them.
  YGNodeStyleSetPadding(node, YGEdgeLeft, 0.0f);
  YGNodeStyleSetPadding(node, YGEdgeRight, 0.0f);
  YGNodeStyleSetPadding(node, YGEdgeTop, 0.0f);
  YGNodeStyleSetPadding(node, YGEdgeBottom, 0.0f);
  YGNodeStyleSetBorder(node, YGEdgeLeft, 0.0f);
  YGNodeStyleSetBorder(node, YGEdgeRight, 0.0f);
  YGNodeStyleSetBorder(node, YGEdgeTop, 0.0f);
  YGNodeStyleSetBorder(node, YGEdgeBottom, 0.0f);
  YGNodeStyleSetAlignItems(node, YGAlignAuto);
  YGNodeStyleSetAlignSelf(node, YGAlignAuto);
  YGNodeStyleSetAlignContent(node, YGAlignFlexStart);
  YGNodeStyleSetJustifyContent(node, YGJustifyFlexStart);
  YGNodeStyleSetGap(node, YGGutterAll, 0.0f);
  YGNodeStyleSetFlexWrap(node, YGWrapNoWrap);
  YGNodeStyleSetFlexBasisAuto(node);
  YGNodeStyleSetFlexShrink(node, 1.0f);
}

YGNodeRef buildSuperDeepStyled(YGConfigRef config, int depth, int nodesPerLevel) {
  std::vector<YGNodeRef> children;
  for (int d = 0; d < depth; d++) {
    YGNodeRef container = YGNodeNewWithConfig(config);
    YGNodeStyleSetFlexDirection(container, YGFlexDirectionRow);
    YGNodeStyleSetFlexGrow(container, 1.0f);
    YGNodeStyleSetMargin(container, YGEdgeAll, 10.0f);
    applyTaffyIshStyle(container);
    for (size_t i = 0; i < children.size(); i++) {
      YGNodeInsertChild(container, children[i], i);
    }
    children.clear();
    children.push_back(container);
    for (int i = 1; i < nodesPerLevel; i++) {
      YGNodeRef leaf = YGNodeNewWithConfig(config);
      YGNodeStyleSetFlexDirection(leaf, YGFlexDirectionRow);
      YGNodeStyleSetFlexGrow(leaf, 1.0f);
      YGNodeStyleSetMargin(leaf, YGEdgeAll, 10.0f);
      applyTaffyIshStyle(leaf);
      children.push_back(leaf);
    }
  }
  YGNodeRef root = children[0];
  return root;
}

std::vector<Workload> buildWorkloads() {
  std::vector<Workload> workloads;

  auto add = [&](const std::string& name,
                 YGNodeRef root,
                 float w,
                 float h,
                 YGDirection dir) {
    Workload wl;
    wl.name = name;
    wl.root = root;
    wl.availableWidth = w;
    wl.availableHeight = h;
    wl.direction = dir;
    collectLeaves(root, wl.leaves);
    workloads.push_back(std::move(wl));
  };

  {
    YGConfigRef c = makeConfig();
    add("flex-deep-row", buildFlexTree(c, 6, 3, true), 1024, 768, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("aspect-ratio-grid", buildAspectRatioGrid(c, 20, 12), 800, 600, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("flex-wrap-200", buildWrap(c, 200), 600, 2000, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("percent-minmax", buildPercentMinMax(c, 7), 500, 900, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    std::vector<YGNodeRef> leaves;
    YGNodeRef root = buildTextHeavy(c, 150, leaves);
    add("text-heavy", root, 400, 4000, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("absolute-300", buildAbsolute(c, 300), 400, 400, YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("rtl-deep", buildFlexTree(c, 6, 3, true), 1024, 768, YGDirectionRTL);
  }
  {
    // The workload where Yoga collapses exponentially vs Taffy. Auto-size
    // (undefined available dims) + deep flexGrow chain.
    YGConfigRef c = makeConfig();
    add("super-deep-50", buildSuperDeep(c, 50, 3), YGUndefined, YGUndefined, YGDirectionLTR);
  }
  {
    // Taffy's yoga binding passes None -> f32::INFINITY (not YGUndefined) to
    // Yoga's C API. Yoga treats INFINITY as a *definite* size (isDefined==true),
    // routing through StretchFit(INFINITY) rather than MaxContent — a different
    // cache regime. This isolates that path (mirrors taffy-compare).
    YGConfigRef c = makeConfig();
    add("super-deep-50-inf", buildSuperDeep(c, 50, 3),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), YGDirectionLTR);
  }
  {
    YGConfigRef c = makeConfig();
    add("super-deep-100", buildSuperDeep(c, 100, 3), YGUndefined, YGUndefined, YGDirectionLTR);
  }
  {
    // Full taffy-binding style on every node + INFINITY available size, to
    // reproduce the taffy-compare super-deep blow-up and isolate the trigger.
    YGConfigRef c = makeConfig();
    add("super-deep-50-styled", buildSuperDeepStyled(c, 50, 3),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), YGDirectionLTR);
  }
  {
    // Default config (pointScaleFactor=0) + INFINITY + bare style: tests whether
    // the absence of pixel-grid rounding (yoga-rs's default) is the blow-up trigger.
    YGConfigRef c = makeConfigDefault();
    add("super-deep-50-inf-ps0", buildSuperDeep(c, 50, 3),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), YGDirectionLTR);
  }
  {
    // Default config + INFINITY + full taffy style: the exact taffy-compare combo.
    YGConfigRef c = makeConfigDefault();
    add("super-deep-50-taffy", buildSuperDeepStyled(c, 50, 3),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(), YGDirectionLTR);
  }
  {
    // Wide shallow tree: the workload cache=64 was suspected of regressing.
    YGConfigRef c = makeConfig();
    add("wide-2000", buildWide(c, 2000), 1024, YGUndefined, YGDirectionLTR);
  }
  {
    // Taffy "huge nested": 10000 fixed-size flex-grow nodes, branching 10,
    // definite viewport. better-yoga vs Taffy is ~parity here (2.21ms vs 2.14ms).
    YGConfigRef c = makeConfig();
    add("huge-nested-10k", buildHugeNested(c, 10000, 10), 1024, 768, YGDirectionLTR);
  }
  {
    // Same huge-nested shape but default config (pointScaleFactor=0), matching
    // taffy-compare's yoga binding — isolates pixel-grid rounding's effect.
    YGConfigRef c = makeConfigDefault();
    add("huge-nested-10k-ps0", buildHugeNested(c, 10000, 10), 1024, 768, YGDirectionLTR);
  }

  return workloads;
}

// Force the whole subtree to be re-laid-out. Yoga only flips the dirty flag
// when a style *value actually changes*, so re-setting the same value is a
// no-op and would leave the cached layout intact. We alternate a tiny padding
// between two values on every node so each call genuinely dirties the node and
// propagates upward — every node is then revisited on the next layout.
void dirtyAll(YGNodeRef node, bool toggle) {
  YGNodeStyleSetPadding(node, YGEdgeBottom, toggle ? 0.0f : 0.01f);
  size_t count = YGNodeGetChildCount(node);
  for (size_t i = 0; i < count; i++) {
    dirtyAll(YGNodeGetChild(node, i), toggle);
  }
}

// Run cold layout until either `maxIters` is reached or `budgetNs` of wall time
// has elapsed, whichever comes first. Auto-scaling to a time budget keeps cheap
// workloads sampled enough for the min to converge while bounding the cost of
// expensive ones. Returns the minimum observed layout time.
int64_t timeColdLayout(Workload& wl, int maxIters, int64_t budgetNs) {
  int64_t best = INT64_MAX;
  int64_t elapsed = 0;
  for (int i = 0; i < maxIters && elapsed < budgetNs; i++) {
    dirtyAll(wl.root, i % 2 == 0);
    auto t0 = steady_clock::now();
    YGNodeCalculateLayout(
        wl.root, wl.availableWidth, wl.availableHeight, wl.direction);
    auto t1 = steady_clock::now();
    int64_t dt = duration_cast<nanoseconds>(t1 - t0).count();
    best = std::min(best, dt);
    elapsed += dt;
  }
  return best;
}

int64_t timeRelayout(Workload& wl, int iters, int64_t /*budgetNs*/) {
  // Initial layout to warm caches.
  YGNodeCalculateLayout(
      wl.root, wl.availableWidth, wl.availableHeight, wl.direction);
  int64_t best = INT64_MAX;
  size_t leafIdx = 0;
  for (int i = 0; i < iters; i++) {
    // Dirty one measure-leaf (if any) to model a localized re-render.
    if (!wl.leaves.empty()) {
      YGNodeRef leaf = wl.leaves[leafIdx % wl.leaves.size()];
      leafIdx++;
      if (YGNodeHasMeasureFunc(leaf)) {
        YGNodeMarkDirty(leaf);
      } else {
        // Non-measure leaf: alternate bottom padding so the value genuinely
        // changes, flipping the dirty flag and propagating up to the root.
        YGNodeStyleSetPadding(leaf, YGEdgeBottom, (i % 2 == 0) ? 0.0f : 0.01f);
      }
    }
    auto t0 = steady_clock::now();
    YGNodeCalculateLayout(
        wl.root, wl.availableWidth, wl.availableHeight, wl.direction);
    auto t1 = steady_clock::now();
    best = std::min(best, duration_cast<nanoseconds>(t1 - t0).count());
  }
  return best;
}

} // namespace

int main(int argc, char* argv[]) {
  // Per-workload cap on iterations; each workload also stops early once its
  // time budget is spent. Default budget ~150ms keeps even a single run fast
  // while giving the min plenty of samples on cheap workloads.
  int maxIters = 100000;
  if (argc >= 2) {
    maxIters = std::atoi(argv[1]);
  }
  int64_t budgetNs = 150'000'000; // 150ms per workload
  if (argc >= 3) {
    budgetNs = (int64_t)std::atoi(argv[2]) * 1'000'000;
  }

  auto workloads = buildWorkloads();

  // Optional argv[3]: only run workloads whose name contains the given filter
  // (for profiling a single workload over a long budget).
  std::string filter = (argc >= 4) ? argv[3] : "";

  printf("%-22s %14s %14s\n", "workload", "cold(us)", "relayout(us)");
  for (auto& wl : workloads) {
    if (!filter.empty() && wl.name.find(filter) == std::string::npos) {
      continue;
    }
    int64_t cold = timeColdLayout(wl, maxIters, budgetNs);
    int64_t relayout = timeRelayout(wl, std::min(maxIters, 5000), budgetNs);
    printf(
        "%-22s %14.3f %14.3f\n",
        wl.name.c_str(),
        cold / 1000.0,
        relayout / 1000.0);
  }
  return 0;
}
