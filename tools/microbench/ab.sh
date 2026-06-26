#!/bin/bash
# tools/microbench/ab.sh — A/B the layout microbenchmark: fork vs pinned baseline.
#
# Both binaries are the SAME Microbench.cpp source (public C API only, unchanged
# since the fork point) linked against different yogacore builds. We interleave
# their runs so thermal/scheduler drift hits both alike, and keep the minimum
# cold-layout time per workload across runs — layout is deterministic, so the
# true cost is the infimum and noise only inflates it.
#
# Usage: tools/microbench/ab.sh            # RUNS=12, ITERS=4000
#        RUNS=20 ITERS=8000 tools/microbench/ab.sh

set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(git -C "$DIR" rev-parse --show-toplevel)"
BASELINE_WT="$ROOT/.baseline"
FORK_BIN="$DIR/build-fork/microbench"
BASE_BIN="$DIR/build-base/microbench"
RUNS="${RUNS:-12}"
MAXITERS="${MAXITERS:-100000}"
BUDGET_MS="${BUDGET_MS:-120}"  # per-workload time budget per run
FLAGS="-Wno-character-conversion"

build() { # <yoga_root> <build_dir>
  cmake -B "$DIR/$2" -S "$DIR" -DCMAKE_BUILD_TYPE=Release -G Ninja \
    -DCMAKE_CXX_FLAGS="$FLAGS" -DYOGA_ROOT="$1" >/dev/null 2>&1
  cmake --build "$DIR/$2" >/dev/null 2>&1
}

echo "[build] fork + baseline microbench..."
build "$ROOT" build-fork
build "$BASELINE_WT" build-base

# Emit "<workload><TAB><cold_us>" for one run.
collect() { "$1" "$MAXITERS" "$BUDGET_MS" | awk 'NR>1 {print $1"\t"$2}'; }

: > /tmp/mb_fork.txt
: > /tmp/mb_base.txt
echo "[run] interleaving $RUNS runs (budget=${BUDGET_MS}ms/workload, min cold time)..."
for ((r=0; r<RUNS; r++)); do
  collect "$FORK_BIN" >> /tmp/mb_fork.txt
  collect "$BASE_BIN" >> /tmp/mb_base.txt
done

min_per() {
  sort -t$'\t' -k1,1 -k2,2n | awk -F'\t' '
    $1!=cur {if(cur!="")print cur"\t"best; cur=$1; best=$2}
    $2<best {best=$2}
    END {if(cur!="")print cur"\t"best}'
}
min_per < /tmp/mb_base.txt > /tmp/mb_base_min.txt
min_per < /tmp/mb_fork.txt > /tmp/mb_fork_min.txt

printf '%-22s %14s %14s %10s\n' "workload" "baseline(us)" "fork(us)" "speedup"
join -t$'\t' /tmp/mb_base_min.txt /tmp/mb_fork_min.txt | awk -F'\t' '
  { sp = ($3>0? $2/$3 : 0);
    printf "%-22s %14.2f %14.2f %9.3fx\n", $1, $2, $3, sp }'
