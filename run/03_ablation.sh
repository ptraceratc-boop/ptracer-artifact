#!/usr/bin/env bash
# E3 -- Figure 6 (ablation of the new techniques): three bars on PolyBench/C gemm (large)
# and two on pyperformance nbody.
#
#   bash run/03_ablation.sh          # needs Intel PT and ptracer/build.sh
#
#   bar 1  PTracer            = the Fast image under a complete Intel PT capture
#   bar 2  PTracer w/o PT     = rewrite.py --log-blocks --sync 0 (a block id at every leader)
#   bar 3  w/o PT and static  = rewrite.py --gt-all (every access logged); PolyBench only
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
REPS="${REPS:-3}"; KERNEL="${KERNEL:-gemm}"; BENCH="${BENCH:-nbody}"
JSON="$RUN_OUT/ablation.json"
echo "== E3 ablation: $KERNEL (large) and $BENCH, median of $REPS =="
echo "Estimated time: ~30 compute-min (plus the Fast image build if E1 has not run)."
quiet_machine_hints
PC="$ROOT/ptracer/offline/pt_capture2"; RW="$ROOT/ptracer/runtime/rewrite.py"
[ -x "$PC" ] || { echo "FATAL: pt_capture2 not built (bash ptracer/build.sh)"; exit 2; }
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}"
LOCK="${PT_LOCK:-$ROOT/.pt_pmu.lock}"; : > "$LOCK"
CORE="${TIMED_CORE:-1}"
export PTLOG_DIR=/dev/null

bash run/lib/build_fast.sh
B="$RUN_OUT/build/ablation"; mkdir -p "$B"
IMG="$ROOT/suites/polybench/targets/${KERNEL}_large"
SPEC="$RUN_OUT/build/spec/${KERNEL}_large.spec.json"
FAST="$RUN_OUT/fast/poly/${KERNEL}_large"
[ -x "$B/blocks" ] || "$PYBIN" "$RW" "$SPEC" "$IMG" -o "$B/blocks" --sink buffer --log-blocks --sync 0
[ -x "$B/gtall"  ] || "$PYBIN" "$RW" "$SPEC" "$IMG" -o "$B/gtall"  --sink buffer --gt-all
PYFAST="$RUN_OUT/fast/pyfast/bin/python3.12"
PYBLK="$(bash run/lib/build_fast.sh pytree pyblocks --log-blocks --sync 0)/bin/python3.12"
PYVAN="$ROOT/suites/pyperformance/cpython-cg/bin/python3.12"
RUN1="$ROOT/suites/pyperformance/pyperf/run_one.py"

last_float() { grep -oE '[0-9]+\.[0-9]+' | tail -1; }
kt()  { taskset -c "$CORE" "$@" 2>/dev/null | last_float; }                       # PolyBench prints its kernel time
pt()  { flock "$LOCK" "$PC" --ptw --no-decode --aux-out /dev/null --sideband "$B/sb.json" --child-core "$CORE" -- "$@" 2>/dev/null | last_float; }
pyk() { PPF_LOOPS=1 PPF_WARM=1 taskset -c "$CORE" "$@" 2>&1 >/dev/null | grep -oE 'KTIME [0-9.]+' | tail -1 | cut -d' ' -f2; }
pypt(){ PPF_LOOPS=1 PPF_WARM=1 flock "$LOCK" "$PC" --ptw --no-decode --aux-out /dev/null --sideband "$B/sb.json" --child-core "$CORE" -- "$@" 2>&1 >/dev/null | grep -oE 'KTIME [0-9.]+' | tail -1 | cut -d' ' -f2; }

declare -a nat b1 b2 b3 pv p1 p2
for r in $(seq 1 "$REPS"); do
    echo "-- repetition $r/$REPS --"
    nat+=("$(kt "$IMG")");   b1+=("$(pt "$FAST")");   b2+=("$(kt "$B/blocks")");   b3+=("$(kt "$B/gtall")")
    pv+=("$(pyk "$PYVAN" "$RUN1" "$BENCH")"); p1+=("$(pypt "$PYFAST" "$RUN1" "$BENCH")"); p2+=("$(pyk "$PYBLK" "$RUN1" "$BENCH")")
done
"$PYBIN" - "$JSON" "$KERNEL" "$BENCH" "${nat[*]}" "${b1[*]}" "${b2[*]}" "${b3[*]}" "${pv[*]}" "${p1[*]}" "${p2[*]}" <<'PY'
import json, sys
f = lambda s: [float(x) for x in s.split()]
_, out, kernel, bench, nat, b1, b2, b3, pv, p1, p2 = sys.argv
bars = [dict(key="bar1", label="PTracer", sublabel="full: PT + static analysis"),
        dict(key="bar2", label="PTracer w/o PT", sublabel="control flow instrumented"),
        dict(key="bar3", label="PTracer w/o PT & Static", sublabel="every memory address")]
suites = [dict(key="polybench", name="PolyBench/C", cell="%s (large)" % kernel, metric="kernel seconds",
               baseline="native", reps=dict(native=f(nat), bar1=f(b1), bar2=f(b2), bar3=f(b3))),
          dict(key="pyperformance", name="pyperformance", cell=bench, metric="KTIME seconds",
               baseline="vanilla", reps=dict(vanilla=f(pv), bar1=f(p1), bar2=f(p2)))]
json.dump(dict(bars=bars, suites=suites), open(out, "w"), indent=1)
PY
"$PYBIN" figures/ablation.py --data "$JSON" --outdir "$RUN_OUT"
echo "== E3 done: $RUN_OUT/ablation.png (Figure 6) =="
