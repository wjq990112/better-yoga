#!/bin/bash
# tools/taffy-compare/run-three-way.sh — upstream-yoga vs better-yoga vs Taffy.
#
# Three-way comparison. Taffy's flexbox bench A/Bs a `yoga` binding against
# Taffy 0.7 in one process; the `yoga` crate is selected via the
# [patch.crates-io] line in benches/Cargo.toml. We run the bench twice — once
# pointing at yoga-rs-original (upstream Yoga, no fast-path / no dynamic cache)
# and once at yoga-rs-patched (better-yoga) — saving each under a named
# criterion baseline, then merge the two runs into a three-column table.
#
# Both yoga variants build with -O2 -flto=thin (setup.sh patches build.rs), so
# upstream-vs-better-yoga isolates *algorithm* from *build* differences. Taffy
# uses its shipped release profile (opt-level 3, no LTO) — what a user gets.
#
# Assumes setup.sh has been run (both yoga-rs-original and yoga-rs-patched
# prepared). Quick mode by default; FULL=1 for full criterion samples.
set -euo pipefail

export PATH="/opt/homebrew/bin:$PATH"
TAFFY="${TAFFY:-$HOME/Workspace/taffy}"
WORKSPACE="${WORKSPACE:-$HOME/Workspace}"
YOGA_ORIGINAL="$WORKSPACE/yoga-rs-original"
YOGA_PATCHED="$WORKSPACE/yoga-rs-patched"
BENCH_CARGO="$TAFFY/benches/Cargo.toml"

# `small` pulls in super-deep/50 + huge-nested/1000. We deliberately omit
# `large` (huge-nested/100000, super-deep/200): upstream Yoga's cyclic cache
# eviction explodes there (super-deep/100 alone is ~46s), so /200 would take
# many minutes per sample for no extra insight.
FEATURES="${FEATURES:-yoga yoga-super-deep small}"
BENCH="${BENCH:-flexbox}"
if [ -n "${FULL:-}" ]; then
  ARGS=()                 # criterion defaults: 100 samples, 5s each
else
  ARGS=(--quick --noplot) # ~10 samples, fast
fi

if [ ! -d "$YOGA_ORIGINAL/src/yoga/yoga" ] || [ ! -d "$YOGA_PATCHED/src/yoga/yoga" ]; then
  echo "missing yoga-rs-original / yoga-rs-patched — run setup.sh first" >&2
  exit 1
fi

set_patch() {
  # Rewrite the `yoga = { path = "..." }` line under [patch.crates-io].
  python3 - "$BENCH_CARGO" "$1" <<'PY'
import re, sys
path, target = sys.argv[1], sys.argv[2]
s = open(path).read()
new = re.sub(
    r'(^yoga = \{ path = )"[^"]*"( \})',
    lambda m: f'{m.group(1)}"{target}"{m.group(2)}',
    s, count=1, flags=re.M)
if new == s:
    sys.exit("patch line not found / unchanged in " + path)
open(path, 'w').write(new)
PY
}

run_variant() {
  local baseline="$1" target="$2" label="$3"
  set_patch "$target"
  echo
  echo "=================================================================="
  echo "  $label  (patch -> $target, baseline: $baseline)"
  echo "=================================================================="
  # cc crate's rerun-if-changed only watches src/yoga/yoga, so switching the
  # patch path can leave a stale libyoga.a fingerprint. Clear yoga build
  # artifacts to force a fresh compile of the selected variant.
  rm -rf "$TAFFY/benches/target/release/build/yoga-"* \
         "$TAFFY/benches/target/release/.fingerprint/yoga-"* 2>/dev/null || true
  ( cd "$TAFFY/benches" \
      && cargo bench --features "$FEATURES" --bench "$BENCH" -- "${ARGS[@]}" --save-baseline "$baseline" )
}

run_variant upstream    "$YOGA_ORIGINAL" "UPSTREAM YOGA (no fast-path, no dynamic cache)"
run_variant better-yoga "$YOGA_PATCHED"  "BETTER-YOGA (LTO + edge fast-path + dynamic cache)"

echo
echo "=================================================================="
echo "  Merging baselines into three-way table"
echo "=================================================================="
python3 - "$TAFFY/benches/target/criterion" <<'PY'
import json, os, sys, glob
from collections import OrderedDict

base = sys.argv[1]
# criterion stores each result at:
#   <base>/<group>/<id>/<param>/<baseline>/estimates.json
# <id> and <param> are SEPARATE directory levels — e.g. the bench id
# "Yoga /100" is laid out as ".../Yoga /100/..." = the two dirs "Yoga " then "100".
# So the baseline dir sits at depth 3, not 2 (the original glob missed everything).
# data[(group, param)] = {("yoga"|"taffy", "upstream"|"better-yoga"): mean_ns}
data = OrderedDict()

def classify(idpart):
    if idpart.startswith("Yoga"):
        return "yoga"
    if idpart.startswith("Taffy 0.7"):
        return "taffy"
    return None

for baseline in ("upstream", "better-yoga"):
    for est in glob.glob(f"{base}/*/*/*/{baseline}/estimates.json"):
        parts = est[len(base)+1:].split("/")
        group, idpart, param = parts[0], parts[1], parts[2]
        engine = classify(idpart)
        if engine is None:
            continue
        mean = json.load(open(est))["mean"]["point_estimate"]  # ns
        data.setdefault((group, param), {})[(engine, baseline)] = mean

order = ["yoga 'huge nested'", "Wide tree", "Deep tree (random size)",
         "Deep tree (auto size)", "super deep"]

def fmt(ns):
    if ns is None:
        return "—"
    if ns >= 1e9:
        return f"{ns/1e9:.2f} s"
    if ns >= 1e6:
        return f"{ns/1e6:.2f} ms"
    return f"{ns/1e3:.1f} µs"

def speedup(a, b):  # how much faster b is than a (b smaller)
    if not a or not b:
        return ""
    r = a / b
    if r >= 1.05:
        return f"{r:.1f}x faster"
    if r <= 0.95:
        return f"{1/r:.1f}x slower"
    return "~parity"

def psort(kv):
    p = kv[0]
    return int(p) if p.isdigit() else 0

print(f"| scenario | upstream yoga | better-yoga | Taffy 0.7 | better-yoga vs upstream |")
print(f"|---|---|---|---|---|")
for group in order:
    rows = [(p, v) for (g, p), v in data.items() if g == group]
    for param, v in sorted(rows, key=psort):
        up = v.get(("yoga", "upstream"))
        by = v.get(("yoga", "better-yoga"))
        # Taffy is identical code in both runs; take the min (noise floor).
        tfs = [x for x in (v.get(("taffy", "upstream")), v.get(("taffy", "better-yoga"))) if x]
        tf = min(tfs) if tfs else None
        print(f"| {group} /{param} | {fmt(up)} | {fmt(by)} | {fmt(tf)} | {speedup(up, by)} |")

# taffy consistency check across the two runs
print()
print("Taffy 0.7 consistency across runs (min used in table):")
mism = 0
for group in order:
    rows = [(p, v) for (g, p), v in data.items() if g == group]
    for param, v in sorted(rows, key=psort):
        tu = v.get(("taffy", "upstream"))
        tb = v.get(("taffy", "better-yoga"))
        if tu and tb:
            r = max(tu, tb) / min(tu, tb)
            flag = "  <-- >15% noise" if r > 1.15 else ""
            print(f"  {group} /{param}: {fmt(tu)} vs {fmt(tb)} (ratio {r:.2f}){flag}")
            mism += 1 if r > 1.15 else 0
print(f"  {mism} scenario(s) differ >15% (sample_size=10 + P/E-core scheduling noise)")
PY

echo
echo "Done. Baselines saved under target/criterion/<group>/<id>/{upstream,better-yoga}/."
