#!/bin/bash
# tools/bench-ab.sh — A/B compare the fork's layout performance against the
# upstream baseline pinned at the better-yoga fork point.
#
# Both the fork (this working tree) and the baseline (.baseline worktree, fixed
# at FORK_POINT) build the *same* benchmark/ source with the *same* compiler
# flags; only the linked yogacore differs. That keeps the comparison apples to
# apples: any delta is attributable to algorithm changes in the fork.
#
# Usage:
#   tools/bench-ab.sh            # default RUNS=5
#   RUNS=9 tools/bench-ab.sh
#
# Output: per-capture layout median for baseline vs fork, and the speedup ratio
# (>1.0 means the fork is faster). Layout time is the pure algorithm portion
# (tree construction is measured separately by the benchmark and excluded).

set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
BASELINE_WT="$ROOT/.baseline"
# Commit better-yoga forked from (the pre-refactor upstream Yoga). Pinned so the
# baseline never moves as the fork advances.
FORK_POINT="db7fc6d8"
CAPTURES="$ROOT/benchmark/captures"
BENCH_FLAGS="-Wno-character-conversion"  # suppress GTest 1.12.1 / Apple clang 21 mismatch
RUNS="${RUNS:-5}"

ensure_baseline_worktree() {
  if [ ! -d "$BASELINE_WT/.git" ] && [ ! -f "$BASELINE_WT/.git" ]; then
    echo "[setup] creating baseline worktree at $FORK_POINT"
    git worktree add --detach "$BASELINE_WT" "$FORK_POINT"
  fi
}

build_bench() {
  local dir="$1" label="$2"
  echo "[build] $label benchmark (Release)..."
  ( cd "$dir/benchmark" \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -G Ninja \
         -DCMAKE_CXX_FLAGS="$BENCH_FLAGS" >/dev/null 2>&1 \
    && cmake --build build >/dev/null 2>&1 )
}

# Run the binary once, emitting "<capture><TAB><layout_median_ms>" per capture.
run_and_collect() {
  local bin="$1"
  "$bin" "$CAPTURES" 2>&1 | awk '
    /layout: median:/ {
      # line: "<name> layout: median: <x> ms, stddev: <y> ms"
      sub(/ layout: median: /, "\t", $0)
      sub(/ ms,.*/, "", $0)
      print $0
    }
  '
}

ensure_baseline_worktree
build_bench "$ROOT"      "fork"
build_bench "$BASELINE_WT" "baseline"

FORK_BIN="$ROOT/benchmark/build/benchmark"
BASE_BIN="$BASELINE_WT/benchmark/build/benchmark"

# Interleave fork/baseline runs so system-level drift (thermal, scheduler)
# affects both alike and cancels in the paired comparison. Each binary already
# reports the median of 100 internal reps; across RUNS we keep the *minimum*
# such median per capture — layout is deterministic, so the true algorithm time
# is the infimum and observed noise only inflates it.
echo "[run] interleaving $RUNS runs each (min-of-medians)..."
: > /tmp/by_fork.txt
: > /tmp/by_base.txt
for ((r=0; r<RUNS; r++)); do
  run_and_collect "$FORK_BIN" >> /tmp/by_fork.txt
  run_and_collect "$BASE_BIN" >> /tmp/by_base.txt
done

min_per_capture() {
  sort -t$'\t' -k1,1 -k2,2n | awk -F'\t' '
    $1 != cur { if (cur != "") print cur "\t" best; cur=$1; best=$2 }
    $2 < best { best=$2 }
    END { if (cur != "") print cur "\t" best }
  '
}

min_per_capture < /tmp/by_base.txt > /tmp/by_base_min.txt
min_per_capture < /tmp/by_fork.txt > /tmp/by_fork_min.txt

printf '%-28s %12s %12s %10s\n' "capture" "baseline(ms)" "fork(ms)" "speedup"
join -t$'\t' /tmp/by_base_min.txt /tmp/by_fork_min.txt | awk -F'\t' '
  { printf "%-28s %12.4f %12.4f %9.2fx\n", $1, $2, $3, ($3>0?$2/$3:0) }
'
