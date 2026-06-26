#!/bin/bash
# tools/taffy-compare/run.sh — run the Taffy-vs-better-yoga comparison bench.
#
# Assumes setup.sh has been run (Taffy cloned, patched yoga crate wired in).
# Runs Taffy's flexbox bench with the yoga binding pointing at better-yoga
# (LTO + edge fast-path). Quick mode by default; set FULL=1 for full samples.
set -euo pipefail
export PATH="/opt/homebrew/bin:$PATH"
TAFFY="${TAFFY:-$HOME/Workspace/taffy}"
cd "$TAFFY/benches"

FEATURES="${FEATURES:-yoga yoga-super-deep small large}"
BENCH="${BENCH:-flexbox}"
if [ -n "${FULL:-}" ]; then
  ARGS=""          # criterion defaults: 100 samples, 5s each
else
  ARGS="--quick --noplot"  # ~10 samples, fast
fi

cargo bench --features "$FEATURES" --bench "$BENCH" -- $ARGS
