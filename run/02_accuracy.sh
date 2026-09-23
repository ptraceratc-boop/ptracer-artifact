#!/usr/bin/env bash
# E2 -- Section 5.6 (inaccuracy): the same-process oracle on PolyBench/C and pyperformance.
#
#   bash run/02_accuracy.sh          # needs Intel PT and ptracer/build.sh; writes traces
#
# Per cell: `rewrite.py --gt-all' logs every memory access of the Fast plan's image to a
# ground-truth ring during the very run whose Intel PT is captured; `ptrecon --gt-in' then
# compares the reconstruction to that execution record by record and counts unknown and
# wrong records. Cells already in the results file are not re-run.
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
POLY="${POLY_CELLS:-gemm atax jacobi-2d correlation durbin}"
PYB="${PYPERF_CELLS:-nbody richards}"
CELLS="$RUN_OUT/inaccuracy_cells.json"; TABLE="$RUN_OUT/inaccuracy.md"
W="${ACC_TMP:-$RUN_OUT/tmp_accuracy}"
cat <<EOF
== E2 inaccuracy: PolyBench/C ($POLY; small data set) and pyperformance ($PYB) ==
Estimated time: 1-3 compute-h; needs tens of GB under $W (deleted per cell).
Results -> $CELLS, table -> $TABLE
EOF
quiet_machine_hints
PC="$ROOT/ptracer/offline/pt_capture2"; RECON="$ROOT/ptracer/offline/ptrecon"
RW="$ROOT/ptracer/runtime/rewrite.py"; ANALYZE="$ROOT/ptracer/static/analyze.py"
for t in "$PC" "$RECON"; do [ -x "$t" ] || { echo "FATAL: $t not built (bash ptracer/build.sh)"; exit 2; }; done
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}"
LOCK="${PT_LOCK:-$ROOT/.pt_pmu.lock}"; : > "$LOCK"
[ -s "$CELLS" ] || echo '[]' > "$CELLS"
mkdir -p "$W"

have_cell() { "$PYBIN" -c 'import json,sys; sys.exit(0 if any(c["cell"]==sys.argv[2] and c["suite"]==sys.argv[3] for c in json.load(open(sys.argv[1]))) else 1)' "$CELLS" "$1" "$2"; }

# oracle <suite> <cell> <spec> <orig image> <rewritten image> <cwd> <argv...>
oracle() {
    local suite="$1" cell="$2" spec="$3" orig="$4" img="$5" cwd="$6"; shift 6
    have_cell "$cell" "$suite" && { echo "-- $suite/$cell: already measured"; return 0; }
    local d="$W/$suite.$cell"; rm -rf "$d"; mkdir -p "$d"
    echo "-- $suite/$cell: capture Intel PT while the run logs its ground truth --"
    ( cd "$cwd" && flock "$LOCK" env PTLOG_DIR="$d" PTLOG_GT=1 PTLOG_GT_DIR="$d" PPF_LOOPS=1 PPF_WARM=0 \
        "$PC" --ptw --no-decode --aux-out "$d/aux" --sideband "$d/sb.json" \
        --child-core "${TIMED_CORE:-1}" -- "$@" >/dev/null 2>"$d/run.err" ) || {
        echo "capture failed (see $d/run.err) -- is Intel PT available?"; return 1; }
    local gt cv; gt="$(ls -S "$d"/gt.*.bin 2>/dev/null | head -1)"; cv="$(ls -S "$d"/cv.*.bin 2>/dev/null | head -1)"
    [ -n "$gt" ] || { echo "no ground-truth ring produced"; return 1; }
    echo "-- $suite/$cell: reconstruct and compare --"
    "$RECON" --aux "$d/aux" --sideband "$d/sb.json" --spec "$spec" --sitemap "$img.sitemap.json" \
        --orig-image "$orig" ${cv:+--cv "$cv"} --gt-in "$gt" --summary "$d/recon.json" >"$d/recon.log" 2>&1
    "$PYBIN" - "$CELLS" "$d/recon.json" "$suite" "$cell" <<'PY'
import json, sys
cells = json.load(open(sys.argv[1])); g = json.load(open(sys.argv[2])).get("gt") or {}
n = g.get("records_compared", 0)
c = dict(suite=sys.argv[3], cell=sys.argv[4], compared=n, unknown=g.get("unknown", 0), wrong=g.get("wrong", 0))
cells.append(c); json.dump(cells, open(sys.argv[1], "w"), indent=1)
print("   %s/%s: %d records, %.4f%% unknown, %.4f%% wrong" % (c["suite"], c["cell"], n,
      100.0 * c["unknown"] / (n or 1), 100.0 * c["wrong"] / (n or 1)))
PY
    rm -rf "$d"
}

B="$RUN_OUT/build/accuracy"; mkdir -p "$B"
for k in $POLY; do
    img="$ROOT/suites/polybench/targets/${k}_small"
    av="$B/$k.gt.avoid.txt"; [ -f "$av" ] || : > "$av"
    for it in 1 2 3 4 5 6 7 8 9 10; do   # analyze -> rewrite, routing around unpatchable sites until it converges
        if [ ! -s "$B/$k.spec.json" ]; then
            A=""; [ -s "$av" ] && A="--avoid $av"
            "$PYBIN" "$ANALYZE" "$img" --mode fast -o "$B/$k.spec.json" $A || exit 1
        fi
        [ -x "$B/$k.gt" ] && break
        "$PYBIN" "$RW" "$B/$k.spec.json" "$img" -o "$B/$k.gt" --sink buffer --gt-all && break
        [ -f "$B/$k.gt.unpatched" ] || exit 1
        grep -oE '^0x[0-9a-fA-F]+' "$B/$k.gt.unpatched" >> "$av"; sort -u -o "$av" "$av"; rm -f "$B/$k.spec.json" "$B/$k.gt"
        [ "$it" = 10 ] && { echo "$k: oracle image did not converge"; exit 1; }
    done
    oracle PolyBench/C "$k" "$B/$k.spec.json" "$img" "$B/$k.gt" "$B" "$B/$k.gt"
done
PYGT="$(bash run/lib/build_fast.sh pytree pygt --gt-all)"
CPY="$ROOT/suites/pyperformance/cpython-cg/bin/python3.12"
for b in $PYB; do
    oracle pyperformance "$b" "$RUN_OUT/build/spec/python3.12.gt.spec.json" "$CPY" "$PYGT/bin/python3.12" \
        "$B" "$PYGT/bin/python3.12" "$ROOT/suites/pyperformance/pyperf/run_one.py" "$b"
done
"$PYBIN" run/lib/inaccuracy_table.py "$CELLS" > "$TABLE"
cat "$TABLE"
echo "== E2 done: $TABLE (Section 5.6) =="
