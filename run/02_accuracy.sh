#!/usr/bin/env bash
# Section 5.6 (inaccuracy of the reconstructed memory trace), Fast and Fast-PTWRITE, PolyBench/C (30) and
# pyperformance (94):
#
#   bash docker/run.sh run/02_accuracy.sh [--dry-run]
#
# Per (configuration, benchmark) cell:
#   * the images are those of Figure 5's Fast / Fast-PTWRITE bars (run/lib/build_wp.sh, run/lib/build_ptw.sh; built
#     here if absent), each rebuilt from the SAME spec with the same rewriter flags plus `--gt-all' (run/lib/acc_gt.sh):
#     every memory-accessing instruction also writes {address, ip} to a lossless in-memory ground-truth ring.  The
#     probe adds no Intel PT packet, so the traced run is its own ground truth (no second run);
#   * one Intel PT capture of the run, then `ptrecon --gt-in' reconstructs the trace and aligns it with the ground
#     truth (run/lib/acc_cell.sh);
#   * inaccuracy = (unknown + wrong + unpaired records) / ground-truth records: an upper bound on the normalised
#     Levenshtein distance between the two traces (run/lib/acc_summary.py).
# Settings: PolyBench _large inputs; pyperformance run_one_full.py with fixed loop counts (1 loop, no warm-up),
# PYTHONHASHSEED=0, fixed SOURCE_DATE_EPOCH, precompiled standard library, minimal environment, ASLR off, one core.
# Resumable: cells already in cells.jsonl are kept.  QUICK=1: 3 PolyBench + 3 pyperformance cells.
# Env: ACC_LANES ("core:helper ..." pairs, default 8 lanes), ACC_MODES ("tnt ptw" = Fast, Fast-PTWRITE),
#      ACC_SUITES ("poly py"), ACC_CELLS (explicit cell list), ACC_TMP (per-cell scratch, ~10 GB per lane),
#      ACC_GTMAX (ground-truth bytes of the main thread, default 4 GiB = 268 M records), JOBS (build processes).
# Output: $RUN_OUT/accuracy/{cells.jsonl, inaccuracy.md, inaccuracy.csv, inaccuracy.png}.
set -u
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
A="$RUN_OUT/accuracy"
POLY_ALL="2mm 3mm adi atax bicg cholesky correlation covariance deriche doitgen durbin fdtd-2d floyd-warshall gemm gemver gesummv gramschmidt heat-3d jacobi-1d jacobi-2d lu ludcmp mvt nussinov seidel-2d symm syr2k syrk trisolv trmm"
PY_ALL="$(grep -v '^#' suites/pyperformance/pyperf/bench94.lst | awk '{print $1}' | tr '\n' ' ')"
if [ "${QUICK:-0}" = 1 ]; then
    A="${OUT:-$ROOT/out}/quick/accuracy"
    POLY_ALL="gemm atax jacobi-2d"; PY_ALL="nbody richards json_dumps"
    echo "QUICK=1: 3 PolyBench + 3 pyperformance cells -> $A"
fi
export ACC_DIR="$A" ACC_POLY="${ACC_POLY:-$POLY_ALL}" ACC_PY="${ACC_PY:-$PY_ALL}"
NC=$(nproc)
if [ -z "${ACC_LANES:-}" ]; then
    n=$(( NC / 4 )); [ $n -gt 8 ] && n=8; [ $n -lt 1 ] && n=1
    ACC_LANES=""; for i in $(seq 0 $((n - 1))); do ACC_LANES="$ACC_LANES $((2 + 2 * i)):$((3 + 2 * i))"; done
fi
export ACC_LANES="${ACC_LANES# }"
nl=$(echo $ACC_LANES | wc -w); ncell=0
for s in ${ACC_SUITES:-poly py}; do
    if [ -n "${ACC_CELLS:-}" ]; then c=$(echo $ACC_CELLS | wc -w); elif [ "$s" = poly ]; then c=$(echo $ACC_POLY | wc -w); else c=$(echo $ACC_PY | wc -w); fi
    ncell=$(( ncell + c * $(echo ${ACC_MODES:-tnt ptw} | wc -w) ))
done
cat <<EOF
== Section 5.6: inaccuracy of Fast and Fast-PTWRITE (same-process ground truth) ==
  cells: $ncell (modes: ${ACC_MODES:-tnt ptw}; suites: ${ACC_SUITES:-poly py}), $nl lanes (core:helper): $ACC_LANES
  steps: Figure 5 Fast / Fast-PTWRITE images (skipped if run/fig5.sh built them), ground-truth twins, cells, table
  estimated time on a 96-vCPU machine: Figure 5 images ~0.5 h if absent; twins ~15 min; cells ~$(( (ncell * 9 + nl * 60 - 1) / (nl * 60) )) h on
  $nl lanes (a cell: capture seconds to minutes, reconstruction 1 min-1 h, ~9 min on average); QUICK=1 ~1.5 h in all
  disk and memory: ~10 GB scratch + ~8 GB RAM per lane under ${ACC_TMP:-$A/tmp} (deleted per cell); ~2 GB for the twins under $A/gt and
  ~2 GB for the Figure 5 images under $RUN_OUT (shared with run/fig5.sh)
  results: $A/cells.jsonl -> $A/inaccuracy.{md,csv,png}
EOF
quiet_machine_hints
[ "${1:-}" = "--dry-run" ] && exit 0

PC="$ROOT/ptracer/offline/pt_capture2"; RECON="$ROOT/ptracer/offline/ptrecon"
for t in "$PC" "$RECON" "$ROOT/ptracer/runtime/rt/ptlogmt.so"; do
    [ -e "$t" ] || { echo "FATAL: $t not built (bash ptracer/build.sh)"; exit 2; }; done
grep -q ' pt ' /proc/cpuinfo || grep -q 'intel_pt' /proc/cpuinfo || { echo "FATAL: no Intel PT on this CPU"; exit 2; }
case " ${ACC_MODES:-tnt ptw} " in *" ptw "*) grep -q ptwrite /proc/cpuinfo || echo "WARNING: no PTWRITE on this CPU: Fast-PTWRITE cells will fail (ACC_MODES=tnt skips them)";; esac
mkdir -p "$A"

# 1. Figure 5 Fast and Fast-PTWRITE images (only what is missing is built)
T=""; P=""
case " ${ACC_SUITES:-poly py} " in *" poly "*) T="$T libs poly"; P="$P libs poly";; esac
case " ${ACC_SUITES:-poly py} " in *" py "*) T="$T py"; P="$P py";; esac
POLY_CELLS="$ACC_POLY" bash run/lib/build_wp.sh ld $T interp || echo "WARNING: some Fast images failed (their cells will report it)"
POLY_CELLS="$ACC_POLY" bash run/lib/build_ptw.sh $P || echo "WARNING: some Fast-PTWRITE images failed"
ln -sfn "$RUN_OUT/fastwp/ptld" /ptld 2>/dev/null || [ "$(readlink /ptld)" = "$RUN_OUT/fastwp/ptld" ] \
    || { echo "FATAL: cannot create /ptld -> $RUN_OUT/fastwp/ptld (run in the container)"; exit 2; }

# 2. ground-truth twins
bash run/lib/acc_gt.sh ${ACC_SUITES:-poly py}

# 3. cells, dealt over the lanes; a cell already in cells.jsonl is skipped
RES="$A/cells.jsonl"; touch "$RES"
pending=()
for m in ${ACC_MODES:-tnt ptw}; do for s in ${ACC_SUITES:-poly py}; do
    cells="${ACC_CELLS:-}"; [ -n "$cells" ] || { [ "$s" = poly ] && cells="$ACC_POLY" || cells="$ACC_PY"; }
    for c in $cells; do
        grep -q "\"mode\": \"$m\", \"suite\": \"$s\", \"cell\": \"$c\"," "$RES" || pending+=("$m $s $c")
    done
done; done
read -r -a LANES <<< "$ACC_LANES"
echo "== ${#pending[@]} cells to measure over ${#LANES[@]} lanes (logs: $A/lane<i>.log) =="
lane() {
    local i=$1 j core=${LANES[$1]%%:*} helper=${LANES[$1]##*:}
    for ((j = i; j < ${#pending[@]}; j += ${#LANES[@]})); do
        bash run/lib/acc_cell.sh ${pending[$j]} "$core" "$helper"
    done
}
for i in "${!LANES[@]}"; do lane "$i" > "$A/lane$i.log" 2>&1 & done
wait

# 4. table + figure
"$PYBIN" run/lib/acc_summary.py "$RES" --md "$A/inaccuracy.md" --csv "$A/inaccuracy.csv" --png "$A/inaccuracy.png"
echo "== Section 5.6 done: $A/inaccuracy.md, $A/inaccuracy.png =="
