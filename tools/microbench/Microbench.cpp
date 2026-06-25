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

  printf("%-22s %14s %14s\n", "workload", "cold(us)", "relayout(us)");
  for (auto& wl : workloads) {
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
