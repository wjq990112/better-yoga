/*
 * better-yoga differential layout harness.
 *
 * For a given seed, deterministically builds a random Yoga tree via the stable
 * public C API, lays it out, and prints every node's computed layout
 * (left/top/width/height + overflow/hadNewLayout) as a flat record stream.
 *
 * Built twice — once linked against the fork yogacore, once against the pinned
 * baseline — the two runs over the same seed range must produce byte-identical
 * output. Any diff is a behavior divergence introduced by the fork. This is the
 * safety net that lets structural refactors claim "behavior completely
 * identical": a structural change is only acceptable if difftest stays silent
 * over a large seed sweep.
 *
 * Usage: difftest <startSeed> <count>   # prints records for seeds [start, start+count)
 */

#include <cstdint>
#include <cstdio>
#include <vector>

#include <yoga/Yoga.h>

namespace {

// Small deterministic PRNG (xorshift32) so fork and baseline see identical
// sequences for the same seed without depending on libc rand() differences.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  uint32_t range(uint32_t n) { return n ? next() % n : 0; }
  float prob() { return (float)(next() & 0xffffff) / (float)0x1000000; }
  bool chance(uint32_t pct) { return range(100) < pct; }
};

YGFlexDirection fd(Rng& r) {
  switch (r.range(4)) {
    case 0: return YGFlexDirectionColumn;
    case 1: return YGFlexDirectionColumnReverse;
    case 2: return YGFlexDirectionRow;
    default: return YGFlexDirectionRowReverse;
  }
}

YGAlign align(Rng& r) {
  switch (r.range(7)) {
    case 0: return YGAlignFlexStart;
    case 1: return YGAlignCenter;
    case 2: return YGAlignFlexEnd;
    case 3: return YGAlignStretch;
    case 4: return YGAlignBaseline;
    case 5: return YGAlignSpaceBetween;
    default: return YGAlignSpaceAround;
  }
}

YGJustify justify(Rng& r) {
  switch (r.range(6)) {
    case 0: return YGJustifyFlexStart;
    case 1: return YGJustifyCenter;
    case 2: return YGJustifyFlexEnd;
    case 3: return YGJustifySpaceBetween;
    case 4: return YGJustifySpaceAround;
    default: return YGJustifySpaceEvenly;
  }
}

float dim(Rng& r) {
  // Mix of small/large/fractional values.
  return (float)r.range(500) + (r.chance(40) ? r.prob() : 0.0f);
}

void buildTree(Rng& r, YGConfigConstRef cfg, YGNodeRef node, int depth) {
  YGNodeStyleSetFlexDirection(node, fd(r));
  if (r.chance(60)) YGNodeStyleSetJustifyContent(node, justify(r));
  if (r.chance(60)) YGNodeStyleSetAlignItems(node, align(r));
  if (r.chance(30)) YGNodeStyleSetAlignContent(node, align(r));
  if (r.chance(25)) YGNodeStyleSetFlexWrap(node, r.chance(50) ? YGWrapWrap : YGWrapWrapReverse);
  if (r.chance(40)) YGNodeStyleSetAlignSelf(node, align(r));
  if (r.chance(30)) YGNodeStyleSetGap(node, YGGutterAll, (float)r.range(20));

  // Sizing: points, percent, auto, or unset.
  switch (r.range(4)) {
    case 0: YGNodeStyleSetWidth(node, dim(r)); break;
    case 1: YGNodeStyleSetWidthPercent(node, (float)r.range(120)); break;
    case 2: YGNodeStyleSetWidthAuto(node); break;
    default: break;
  }
  switch (r.range(4)) {
    case 0: YGNodeStyleSetHeight(node, dim(r)); break;
    case 1: YGNodeStyleSetHeightPercent(node, (float)r.range(120)); break;
    case 2: YGNodeStyleSetHeightAuto(node); break;
    default: break;
  }
  if (r.chance(25)) YGNodeStyleSetMinWidth(node, (float)r.range(100));
  if (r.chance(25)) YGNodeStyleSetMaxWidth(node, (float)r.range(400) + 100);
  if (r.chance(25)) YGNodeStyleSetMinHeight(node, (float)r.range(100));
  if (r.chance(25)) YGNodeStyleSetMaxHeight(node, (float)r.range(400) + 100);
  if (r.chance(30)) YGNodeStyleSetAspectRatio(node, 0.5f + (float)r.range(30) / 10.0f);

  if (r.chance(50)) YGNodeStyleSetFlexGrow(node, (float)r.range(4));
  if (r.chance(50)) YGNodeStyleSetFlexShrink(node, (float)r.range(4));
  if (r.chance(30)) YGNodeStyleSetFlexBasis(node, dim(r));

  for (YGEdge e : {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom}) {
    if (r.chance(30)) YGNodeStyleSetMargin(node, e, (float)r.range(30));
    if (r.chance(30)) YGNodeStyleSetPadding(node, e, (float)r.range(30));
    if (r.chance(20)) YGNodeStyleSetBorder(node, e, (float)r.range(10));
  }

  if (r.chance(15)) {
    YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    if (r.chance(60)) YGNodeStyleSetPosition(node, YGEdgeLeft, (float)r.range(200));
    if (r.chance(60)) YGNodeStyleSetPosition(node, YGEdgeTop, (float)r.range(200));
  }
  if (r.chance(8)) YGNodeStyleSetDisplay(node, YGDisplayNone);

  int maxChildren = depth >= 4 ? 0 : (depth >= 2 ? 4 : 6);
  int children = (int)r.range((uint32_t)maxChildren + 1);
  for (int i = 0; i < children; i++) {
    YGNodeRef child = YGNodeNewWithConfig(cfg);
    YGNodeInsertChild(node, child, (size_t)i);
    buildTree(r, cfg, child, depth + 1);
  }
}

void dumpNode(YGNodeRef node, int id) {
  // Quantize to a fixed format so floating-point prints identically.
  printf(
      "%d %.4f %.4f %.4f %.4f %d %d\n",
      id,
      (double)YGNodeLayoutGetLeft(node),
      (double)YGNodeLayoutGetTop(node),
      (double)YGNodeLayoutGetWidth(node),
      (double)YGNodeLayoutGetHeight(node),
      YGNodeLayoutGetHadOverflow(node) ? 1 : 0,
      (int)YGNodeGetChildCount(node));
}

int gId = 0;
void dumpTree(YGNodeRef node) {
  dumpNode(node, gId++);
  size_t n = YGNodeGetChildCount(node);
  for (size_t i = 0; i < n; i++) {
    dumpTree(YGNodeGetChild(node, i));
  }
}

void freeTree(YGNodeRef node) {
  while (YGNodeGetChildCount(node) > 0) {
    YGNodeRef child = YGNodeGetChild(node, 0);
    YGNodeRemoveChild(node, child);
    freeTree(child);
  }
  YGNodeFree(node);
}

} // namespace

int main(int argc, char* argv[]) {
  uint32_t startSeed = 1;
  uint32_t count = 1000;
  if (argc >= 2) startSeed = (uint32_t)std::atoi(argv[1]);
  if (argc >= 3) count = (uint32_t)std::atoi(argv[2]);

  for (uint32_t seed = startSeed; seed < startSeed + count; seed++) {
    Rng r(seed);
    YGConfigRef cfg = YGConfigNew();
    YGConfigSetPointScaleFactor(cfg, r.chance(50) ? 2.0f : 3.0f);
    if (r.chance(50)) YGConfigSetErrata(cfg, YGErrataAll);

    YGNodeRef root = YGNodeNewWithConfig(cfg);
    buildTree(r, cfg, root, 0);

    float aw = r.chance(20) ? YGUndefined : (float)(r.range(1000) + 50);
    float ah = r.chance(20) ? YGUndefined : (float)(r.range(1000) + 50);
    YGDirection dir = r.chance(30) ? YGDirectionRTL : YGDirectionLTR;

    YGNodeCalculateLayout(root, aw, ah, dir);

    printf("# seed %u\n", seed);
    gId = 0;
    dumpTree(root);

    freeTree(root);
    YGConfigFree(cfg);
  }
  return 0;
}
