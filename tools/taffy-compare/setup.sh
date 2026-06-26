#!/bin/bash
# tools/taffy-compare/setup.sh — build the Taffy-vs-better-yoga comparison harness.
#
# Reuses Taffy's official benches (which already A/B a yoga binding against
# Taffy on the same random trees and the same criterion timing) but repoints
# the crates.io `yoga` crate at a local patched copy whose vendored C++ is
# better-yoga (with the edge fast-path) and whose cc flags add -flto=thin to
# reproduce better-yoga's shipped Release build. Patches yoga-rs's Rust FFI to
# track better-yoga's newer C API (YGNodeStyleGetGap now returns YGValue;
# YGUnit gained sizing keywords).
#
# Usage: tools/taffy-compare/setup.sh
set -euo pipefail

BETTER_YOGA="$(git -C "$(dirname "$0")/../.." rev-parse --show-toplevel)"
WORKSPACE="${WORKSPACE:-$HOME/Workspace}"
TAFFY="$WORKSPACE/taffy"
YOGA_RS="$WORKSPACE/yoga-rs-patched"
export PATH="/opt/homebrew/bin:$PATH"

echo "[1/4] checking rust toolchain"
command -v cargo >/dev/null || { echo "cargo not found (brew install rust)" >&2; exit 1; }

echo "[2/4] cloning Taffy"
if [ ! -d "$TAFFY/.git" ]; then
  git clone --depth 1 https://github.com/DioxusLabs/taffy.git "$TAFFY"
fi

# Trigger cargo to fetch the yoga crate (it's an optional dep).
echo "[3/4] fetching crates.io yoga crate (so we can patch it locally)"
( cd "$TAFFY/benches" && cargo build --features yoga 2>&1 | tail -2 >/dev/null ) || true
SRC="$(find ~/.cargo/registry/src -maxdepth 2 -type d -name 'yoga-0.5.0' 2>/dev/null | head -1)"
[ -n "$SRC" ] || { echo "could not locate fetched yoga-0.5.0 crate" >&2; exit 1; }

echo "[4/4] preparing patched yoga crate at $YOGA_RS"
rm -rf "$YOGA_RS"
cp -R "$SRC" "$YOGA_RS"

# Replace vendored upstream yoga C++ with better-yoga (includes fast-path).
rm -rf "$YOGA_RS/src/yoga/yoga"
cp -R "$BETTER_YOGA/yoga" "$YOGA_RS/src/yoga/yoga"

# --- apply yoga-rs patches to track better-yoga's C API + enable LTO ---
DIR="$(dirname "$0")"
patch -d "$YOGA_RS" -p1 < "$DIR/patches/01-build-lto.patch"
patch -d "$YOGA_RS" -p1 < "$DIR/patches/02-gap-getter-ygvalue.patch"
patch -d "$YOGA_RS" -p1 < "$DIR/patches/03-yunit-keywords.patch"

# Point Taffy's benches at the local patched yoga crate.
BENCH_CARGO="$TAFFY/benches/Cargo.toml"
grep -q '^\[patch.crates-io\]' "$BENCH_CARGO" || cat >> "$BENCH_CARGO" <<EOF

# better-yoga: local patched yoga crate (better-yoga C++ + LTO + FFI fixes)
[patch.crates-io]
yoga = { path = "$YOGA_RS" }
EOF

echo
echo "Harness ready. Run:"
echo "  cd $TAFFY/benches && cargo bench --features 'yoga yoga-super-deep small large' --bench flexbox -- --quick --noplot"
