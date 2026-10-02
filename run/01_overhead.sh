#!/usr/bin/env bash
# E1 -- Figure 5 (runtime overhead), PTracer's bars: HiFi, HiFi-PTWRITE, Fast, Fast-PTWRITE on PolyBench/C (30 kernels),
# pyperformance (94 benchmarks), Memcached and Rust Stream (6 runtimes); HiFi, HiFi-PTWRITE, Fast and Fast-PTWRITE on
# Node.js (Web Tooling, 18) and Java (Renaissance, 25).  The reviewer entry point is run/fig5.sh
# (sets the defaults); the traditional tracers are run/fig5_traditional.sh.
#
#   bash run/01_overhead.sh   # needs Intel PT, the Pin 4.4 kit and ptracer/build.sh; runs as root in the
#                             # container (it creates the /ptld loader link)
#
# Whole-process configuration (the final Figure 5 setup) for the four non-JIT suites:
# HiFi = Intel Pin 4.4 JIT + the HiFi Pintool (-inter_trace_liveness) + software-buffer sink, with a plan that
#        covers the main image, every library it maps and the loader (data/plans/wp);
# Fast = every mapped image rewritten (run/lib/build_wp.sh: trampolines, E9Patch CFR, software-buffer sink), the
#        rewritten loader as the program interpreter, the pthread shim preloaded, captured with `--sideband'
#        and `--map-poll-gate';
# both under a complete Intel PT capture whose streams are discarded. Rows already in the CSV are
# not re-measured, so the script can be resumed.
#
# Cores (all optional):
#   TIMED_LANES="5" HELPER_LANES="12-15"  parallel lanes for the single-threaded PolyBench / pyperformance cells:
#       lane i times on core TIMED_LANES[i] with helper cores HELPER_LANES[i] (e.g. TIMED_LANES="1 4 7 10"
#       HELPER_LANES="2 5 8 11"); cells are dealt round-robin.  One lane = the serial driver.
#   RUST_CORES="4-7" MC_SRV_CORES="4,5,6,7" MC_CLI_CORES="8-11"  the multi-threaded Rust / Memcached cells.
#   NONJIT_CONFIGS (vanilla,pinhifi,fast), REPS (3: non-JIT suites), JIT_REPS (5: Node), JREPS (3: Java), POLY_CELLS, PYPERF_LIST, SKIP_JIT=1.
#
# JIT suites, whole-program Fast (after the HiFi JIT sweeps): JIT_FAST_CONFIGS (default e9fast,e9fast_ptw;
# empty = skip).  e9fast = the runtime's ELF images rewritten (node; libjvm + 12 JDK libraries; libc/libm/libstdc++/
# libgcc_s) plus trampolines patched into the JIT code by the runtime hook, software-buffer sink throughout.
# e9fast_ptw (the Fast-PTWRITE bar) = node / libjvm.so rebuilt with the mixed sink (run/lib/jitwp/build_ptw.sh:
# PTWRITE for the budgeted sites of data/ptw_sites/jit, the buffer for the rest and for JIT code); only its capture
# enables PTW packets.  The images are built once by
# run/lib/jitwp/build_stage.sh (into JITWP_DIR, default $RUN_OUT/jitwp; seeded from data/avoid/jit; ~20-40 min
# on all cores) when JITWP_DIR/stage/manifest.json is absent; then run/lib/jitwp/sweep_jit_fast.py times vanilla, e9fast
# and e9fast_ptw, alternated inside every repetition, JIT_REPS repetitions for Node and JREPS for Java, one untimed warm-up
# e9fast process per benchmark first (it fills the JIT plan cache; Java: drain bound JAVA_WARM_DRAIN_MS=1800000); these
# warm-ups run as an untimed pass before the timed rows, several at once on disjoint cores (NODE_WARM_SLOTS,
# JAVA_WARM_SLOTS; empty = inline before each benchmark).
# Both arms of that sweep share: Node -- PTJ_NODE_V8FLAGS ("--no-short-builtin-calls --max-semi-space-size=128
# --single-threaded-gc"), 5 untimed + 10 timed iterations, a pending-plan wait before the window (WTB_DRAIN_MS=120000);
# Java -- JAVA_XFLAGS ("-XX:UseAVX=2"), java_iters.txt, the ptjdrain boundary drain (JAVA_DRAIN_MS=1800000).
# Cores: NODE_JIT_CORES (16,17,18,19), JAVA_JIT_CORES (8,9,10,11,16,17,18,19) for the analyzer services; JIT_FAST_NODE_CELLS /
# JIT_FAST_JAVA_CELLS (comma lists) narrow the cells.  Rows: config vanilla/e9fast/e9fast_ptw, label wpfast_r<k>.
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
CSV="$RUN_OUT/overhead.csv"
# Rows of a configuration measured by an older version of the artifact are dropped here and re-measured (run/lib/row_versions.py).
"$PYBIN" run/lib/row_versions.py prune "$RUN_OUT"
# HiFi-PTWRITE rows of an older version (dropped above) are re-measured with a reduced plan by default -- 1
# repetition (PTW_REMEASURE_REPS); PolyBench PTW_REMEASURE_POLY (8 kernels), Rust all 6 runtimes, Memcached 1 run,
# pyperformance and Node.js the full suites; Java as in the first run (1 repetition).  PTW_REMEASURE_FULL=1: the full
# plan (REPS / JIT_REPS, every cell).  The figure draws such a bar from the cells it has (the table gives cells/reps).
PTW_RM=0
if [ "${PTW_REMEASURE_FULL:-0}" != 1 ] && "$PYBIN" run/lib/row_versions.py remeasure "$RUN_OUT" pinhifi_ptw; then
    case ",${NONJIT_CONFIGS:-vanilla,pinhifi,fast}," in *,pinhifi_ptw,*) PTW_RM=1;; esac
fi
PTW_RMR="${PTW_REMEASURE_REPS:-1}"
PTW_RMPOLY="${PTW_REMEASURE_POLY:-2mm atax correlation gemm jacobi-2d lu seidel-2d trmm}"
REPS="${REPS:-3}"; JIT_REPS="${JIT_REPS:-5}"; JREPS="${JREPS:-3}"
read -r -a TL <<< "${TIMED_LANES:-${TIMED_CORE:-5}}"
read -r -a HL <<< "${HELPER_LANES:-${HELPER_CORES:-12-15}}"
[ "${#TL[@]}" = "${#HL[@]}" ] || { echo "FATAL: TIMED_LANES and HELPER_LANES differ in length"; exit 2; }
echo "== E1 runtime overhead: $REPS repetitions per non-JIT cell (Node $JIT_REPS, Java $JREPS), configurations alternated =="
echo "Lanes: ${#TL[@]} (timed ${TL[*]}; helpers ${HL[*]}).  Estimated time: ~1-1.5 days from scratch (README per-suite table),"
echo "plus several compute-h (all cores) to build the whole-process Fast images once."
echo "Results -> $CSV, figure -> $RUN_OUT/six_suite_overhead.png (redrawn as results arrive)"
progress_figure
quiet_machine_hints
PIN="${PIN_ROOT:-$ROOT/third_party/pin-4.4}"
[ -x "$PIN/pin" ] || { echo "FATAL: Pin 4.4 kit not found at $PIN (set PIN_ROOT)"; exit 2; }
[ -x "$ROOT/ptracer/offline/pt_capture2" ] || { echo "FATAL: not built (bash ptracer/build.sh)"; exit 2; }

# Whole-process Fast images; their main images name /ptld/fast/ld.so (the rewritten loader) as interpreter.
bash run/lib/build_wp.sh ${WP_TARGETS:-} || echo "WARNING: some whole-process Fast images were not built (see [build_wp] above); cells that map them run with the original image there"
FWP="$RUN_OUT/fastwp"
# Fast-PTWRITE images (mixed sink: PTWRITE for the shipped budgeted sites, the software buffer for the rest).
PWP="$RUN_OUT/fastwp_ptw"
case ",${NONJIT_CONFIGS:-vanilla,pinhifi,fast}," in *,fast_ptw,*) bash run/lib/build_ptw.sh || echo "WARNING: Fast-PTWRITE build incomplete (see above)";; esac
if [ "$(readlink /ptld 2>/dev/null)" != "$FWP/ptld" ]; then
    ln -sfn "$FWP/ptld" /ptld 2>/dev/null || { echo "FATAL: cannot create /ptld -> $FWP/ptld (run as root, e.g. in the container)"; exit 2; }
fi
[ -x /ptld/fast/ld.so ] || { echo "FATAL: /ptld/fast/ld.so missing"; exit 2; }

# The Pintool matches an image by absolute path; the plans are stored repo-relative.  Every whole-process plan
# gets the loader's section appended.
PLANS_ABS="$RUN_OUT/build/plans"; PLANS_WP="$RUN_OUT/build/plans_wp"; mkdir -p "$PLANS_ABS" "$PLANS_WP"
absplan() { sed -E "s#^I (suites/)#I $ROOT/\1#" "$1"; }
for p in "$ROOT"/data/plans/*.plan; do absplan "$p" > "$PLANS_ABS/$(basename "$p")"; done
for p in "$ROOT"/data/plans/wp/poly.*.plan "$ROOT"/data/plans/wp/cpython_wp.plan "$ROOT"/data/plans/rust.plan "$ROOT"/data/plans/mc.plan; do
    { absplan "$p"; tail -n +2 "$ROOT/data/plans/wp/ld.so.lib.plan"; } > "$PLANS_WP/$(basename "$p")"
done
PINX="-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 -xyzzy -inter_trace_liveness 1 -spillslot_memop 0"
POLY_ALL="2mm 3mm adi atax bicg cholesky correlation covariance deriche doitgen durbin fdtd-2d floyd-warshall gemm gemver gesummv gramschmidt heat-3d jacobi-1d jacobi-2d lu ludcmp mvt nussinov seidel-2d symm syr2k syrk trisolv trmm"
PYD="$ROOT/suites/pyperformance"
export FIG5_POLY="${POLY_CELLS:-$POLY_ALL}" FIG5_RUST="${RUST_CELLS:-seq rayon rust-ssp std-threads tokio pipeliner}" \
       FIG5_PYPERF_LIST="${PYPERF_LIST:-$PYD/pyperf/bench94.lst}" FIG5_PYRUN="$PYD/pyperf/run_one_full.py" \
       FIG5_PYLOOPS="${FIG5_PYLOOPS-$PYD/pyperf/loops.tsv}" FIG5_PYPLAN=cpython_wp.plan \
       FIG5_PYPATH_VAN="$PYD/wp/extmods:$PYD/wp/site" FIG5_PYPATH_FAST="$FWP/py/pyext:$FWP/py/pysite" \
       FIG5_PYPATH_FAST_PTW="$PWP/py/pyext:$PWP/py/pysite"
RS_OUT="$RUN_OUT/repslice"; mkdir -p "$RS_OUT"
rs() {  # rs <csv> <suite(s)> <reps> [repslice args...]: one repslice run in the whole-process setup
    local csv="$1" su="$2" n="$3"; shift 3
    local fd="$FWP" lib="$FWP/lib" pd="$PWP" plib="$PWP/lib"
    [ "$su" = pyperf ] && { fd="$FWP/py"; lib="$FWP/py/lib"; pd="$PWP/py"; plib="$PWP/py/lib"; }
    PLANS_DIR="$PLANS_WP" FAST_DIR="$fd" FAST_PTW_DIR="$pd" FIG5_FASTLIB="$lib" FIG5_FASTLIB_PTW="$plib" \
        FIG5_OUT="${LANE_OUT:-$RS_OUT}" \
        "$PYBIN" run/lib/repslice.py --suites "$su" --configs "${NONJIT_CONFIGS:-vanilla,pinhifi,fast}" --reps "$n" \
        --pin-root "$PIN" --pin-extra "$PINX" --label v1 --csv "$csv" ${MC_OPS:+--mc-ops "$MC_OPS"} "$@"
}
lanes() {  # lanes <suite> <cells...>: cells dealt round-robin over the lanes, one repslice per lane
    local su="$1"; shift; local cells=("$@") n=${#TL[@]} i j
    if [ "$n" = 1 ]; then TIMED_CORE="${TL[0]}" HELPER_CORES="${HL[0]}" rs "$CSV" "$su" "$REPS" --cells "$(IFS=,; echo "${cells[*]}")"; return; fi
    local parts=()
    for i in $(seq 0 $((n-1))); do
        local mine=()
        for j in $(seq "$i" "$n" $((${#cells[@]}-1))); do mine+=("${cells[$j]}"); done
        [ ${#mine[@]} -gt 0 ] || continue
        parts+=("$RUN_OUT/overhead.lane$i.$su.csv"); mkdir -p "$RS_OUT/lane$i"
        TIMED_CORE="${TL[$i]}" HELPER_CORES="${HL[$i]}" LANE_OUT="$RS_OUT/lane$i" \
            rs "$RUN_OUT/overhead.lane$i.$su.csv" "$su" "$REPS" --cells "$(IFS=,; echo "${mine[*]}")" \
            --no-lock --mutex-inherit --load-gate 0 > "$RUN_OUT/overhead.lane$i.$su.log" 2>&1 &
    done
    wait
    "$PYBIN" run/lib/merge_csv.py "$CSV" "${parts[@]}"
}
read -r -a POLYCELLS <<< "$FIG5_POLY"
read -r -a PYCELLS <<< "$(tr '\n' ' ' < "$FIG5_PYPERF_LIST")"
NJC="${NONJIT_CONFIGS:-vanilla,pinhifi,fast}"
[ "$PTW_RM" = 1 ] && NJC=$(echo ",$NJC," | sed 's/,pinhifi_ptw,/,/; s/^,//; s/,$//')
NONJIT_CONFIGS="$NJC"
[ "${SKIP_POLY:-0}" = 1 ] || lanes poly "${POLYCELLS[@]}"
[ "${SKIP_PYPERF:-0}" = 1 ] || lanes pyperf "${PYCELLS[@]}"
[ "${SKIP_RUST:-0}" = 1 ] || TIMED_CORE="${TL[0]}" HELPER_CORES="${HL[0]}" rs "$CSV" rust "$REPS"
[ "${SKIP_MC:-0}" = 1 ] || TIMED_CORE="${TL[0]}" HELPER_CORES="${HL[0]}" rs "$CSV" mc "$REPS"
if [ "$PTW_RM" = 1 ]; then
    echo "== HiFi-PTWRITE re-measure (reduced; PTW_REMEASURE_FULL=1 for the full plan): $PTW_RMR rep; PolyBench: $PTW_RMPOLY; Rust 6; Memcached; pyperformance all =="
    read -r -a RMPOLY <<< "$PTW_RMPOLY"
    [ "${SKIP_POLY:-0}" = 1 ] || NONJIT_CONFIGS=pinhifi_ptw REPS="$PTW_RMR" lanes poly "${RMPOLY[@]}"
    [ "${SKIP_PYPERF:-0}" = 1 ] || NONJIT_CONFIGS=pinhifi_ptw REPS="$PTW_RMR" lanes pyperf "${PYCELLS[@]}"
    [ "${SKIP_RUST:-0}" = 1 ] || NONJIT_CONFIGS=pinhifi_ptw TIMED_CORE="${TL[0]}" HELPER_CORES="${HL[0]}" rs "$CSV" rust "$PTW_RMR"
    [ "${SKIP_MC:-0}" = 1 ] || NONJIT_CONFIGS=pinhifi_ptw TIMED_CORE="${TL[0]}" HELPER_CORES="${HL[0]}" rs "$CSV" mc "$PTW_RMR"
fi

# Node.js HiFi and HiFi-PTWRITE (Pin 4.4 + the HiFi Pintool with a whole-process plan for node and its native
# libraries, JIT-code plans published through the async Pin bridge), then the JIT suites' whole-program Fast sweep.
# Java HiFi and HiFi-PTWRITE likewise (whole-process plan of the JVM's native images, JIT-code plans published by the
# JVMTI agent through the Pin bridge; run/lib/java_hifi.sh, needs kernel.yama.ptrace_scope=0).
NODE_PRESENT=0
{ [ -e "$ROOT/suites/node/bin/node" ] || [ -e "$ROOT/suites/node/bin/node.part-00" ]; } && NODE_PRESENT=1
if [ "${SKIP_JIT:-0}" = 1 ]; then
    echo "SKIP_JIT=1: Node.js and Java not run"
elif [ "$NODE_PRESENT" = 1 ] && [ "${SKIP_NODE_HIFI:-0}" != 1 ]; then
    if [ "$PTW_RM" = 1 ]; then   # the unchanged configurations as before (rows present: nothing re-run), then HiFi-PTWRITE
        NHC=$(echo ",${NODE_HIFI_CONFIGS:-vanilla,pinhifi,pinhifi_ptw}," | sed 's/,pinhifi_ptw,/,/; s/^,//; s/,$//')
        [ -z "$NHC" ] || NODE_HIFI_CONFIGS="$NHC" bash run/lib/node_hifi.sh "$CSV" || echo "ERROR: the Node.js HiFi sweep did not finish (rows so far are kept; rerun to resume)"
        NODE_HIFI_CONFIGS=pinhifi_ptw JIT_REPS="$PTW_RMR" bash run/lib/node_hifi.sh "$CSV" || echo "ERROR: the Node.js HiFi-PTWRITE re-measure did not finish (rerun to resume)"
    else
        bash run/lib/node_hifi.sh "$CSV" || echo "ERROR: the Node.js HiFi sweep did not finish (rows so far are kept; rerun to resume)"
    fi
fi
if [ "${SKIP_JIT:-0}" != 1 ] && [ "${SKIP_JAVA_HIFI:-0}" != 1 ] && [ -f "$ROOT/suites/java/renaissance/renaissance.jar" ]; then
    bash run/lib/java_hifi.sh "$CSV" || echo "ERROR: the Java HiFi sweep did not finish (rows so far are kept; rerun to resume)"
fi

# The JIT suites, whole-program Fast: build the stage once, then the vanilla/e9fast sweep.
JIT_FAST_CONFIGS="${JIT_FAST_CONFIGS-e9fast,e9fast_ptw}"
if [ "${SKIP_JIT:-0}" != 1 ] && [ -n "$JIT_FAST_CONFIGS" ]; then
    JSU="java"; JB="java"
    if [ -e "$ROOT/suites/node/bin/node" ]; then JSU="node,java"; JB="java node"; fi
    JITWP_DIR="${JITWP_DIR:-$RUN_OUT/jitwp}"; export JITWP_DIR
    JFOK=1
    if [ ! -f "$JITWP_DIR/stage/manifest.json" ]; then
        # a stage that cannot be built skips the JIT Fast rows (the check below names them)
        bash run/lib/jitwp/build_stage.sh $JB || { JFOK=0; echo "ERROR: JIT whole-program Fast stage not built -- Node/Java Fast rows SKIPPED (JIT_FAST_CONFIGS= silences this)"; }
    fi
    # Fast-PTWRITE: node / libjvm.so rewritten again with the mixed sink (data/ptw_sites/jit; minutes) -> $JITWP_DIR/ptw
    if [ "$JFOK" = 1 ] && [[ ",$JIT_FAST_CONFIGS," == *,e9fast_ptw,* ]] && [ ! -f "$JITWP_DIR/ptw/stage/manifest.json" ]; then
        bash run/lib/jitwp/build_ptw.sh $JB || { JIT_FAST_CONFIGS=$(echo ",$JIT_FAST_CONFIGS," | sed 's/,e9fast_ptw,/,/; s/^,//; s/,$//')
            echo "ERROR: JIT Fast-PTWRITE stage not built -- Node/Java Fast-PTWRITE rows SKIPPED"; }
    fi
    # Fast-PTWRITE rows of an older version (dropped above) are re-measured alone: only that arm, PTW_REMEASURE_REPS
    # (1) repetition of every Node.js and Java cell, against the rows already measured; PTW_REMEASURE_FULL=1: the full plan.
    JSWEEP="vanilla,$JIT_FAST_CONFIGS"
    if [ "${PTW_REMEASURE_FULL:-0}" != 1 ] && [[ ",$JIT_FAST_CONFIGS," == *,e9fast_ptw,* ]] \
       && "$PYBIN" run/lib/row_versions.py remeasure "$RUN_OUT" e9fast_ptw \
       && ! "$PYBIN" run/lib/row_versions.py remeasure "$RUN_OUT" e9fast; then
        JSWEEP=e9fast_ptw; JIT_REPS="$PTW_RMR"; JREPS="$PTW_RMR"
        echo "== Node.js/Java Fast-PTWRITE re-measure (e9fast_ptw only, $PTW_RMR rep; PTW_REMEASURE_FULL=1 for the full plan) =="
    fi
    # Untimed warm-up processes first (one per benchmark; they fill the JIT plan cache), several at once on disjoint
    # cores (NODE_WARM_SLOTS / JAVA_WARM_SLOTS "CORES/JITCORES ..."; empty = one inline warm-up before each benchmark's
    # timed rows).  No timed row runs while they do.
    NWS="${NODE_WARM_SLOTS-1/2,3,4,5 6/7,8,9,10 12/13,14,15,20 24/25,26,27,28 30/31,32,33,34 36/37,38,39,40}"
    JWS="${JAVA_WARM_SLOTS-0-3/4,5,6,7,8,9,10,11 24-27/28,29,30,31,32,33,34,35 12-15/16,17,18,19,20,21,22,23 36-39/40,41,42,43,44,45,46,47}"
    [ "${JIT_WARM_PASS:-1}" = 1 ] || { NWS=""; JWS=""; }   # JIT_WARM_PASS=0: inline warm-ups
    if [ "$JFOK" = 1 ] && [ "$JSU" = "node,java" ]; then
        [ -z "$NWS" ] || "$PYBIN" run/lib/jitwp/sweep_jit_fast.py --stage "$JITWP_DIR/stage" --out "$RUN_OUT/jitfast" \
            --csv "$CSV" --suites node --configs "$JSWEEP" --warm-only --warm-slots "$NWS" \
            ${JIT_FAST_NODE_CELLS:+--node-cells "$JIT_FAST_NODE_CELLS"} || echo "WARNING: Node.js warm-up pass incomplete (the sweep warms the rest inline)"
        "$PYBIN" run/lib/jitwp/sweep_jit_fast.py --stage "$JITWP_DIR/stage" --out "$RUN_OUT/jitfast" --csv "$CSV" \
            --suites node --node-reps "$JIT_REPS" --configs "$JSWEEP" ${JIT_FAST_NODE_CELLS:+--node-cells "$JIT_FAST_NODE_CELLS"}
    fi
    # Java: JAVA_LANES (2) concurrent lanes, benchmarks dealt round-robin; lane i runs the JVM on JAVA_LANE_CORES[i] and its
    # analyzer services on JAVA_LANE_JIT[i] (disjoint physical cores; SMT siblings left idle).
    if [ "$JFOK" = 1 ]; then
        read -r -a JLC <<< "${JAVA_LANE_CORES:-0-3 24-27}"; read -r -a JLJ <<< "${JAVA_LANE_JIT:-8,9,10,11,16,17,18,19 28,29,30,31,32,33,34,35}"
        JN="${JAVA_LANES:-2}"; [ "$JN" -le "${#JLC[@]}" ] || JN=${#JLC[@]}
        read -r -a JC <<< "$(echo "${JIT_FAST_JAVA_CELLS:-$(grep -v '^#' run/lib/jitwp/java_iters.txt | awk '{print $1}')}" | tr ',\n' '  ')"
        read -r -a JWA <<< "$JWS"
        parts=()
        for i in $(seq 0 $((JN-1))); do   # warm-up pass: lane i's benchmarks on slots i, i+JN, ... into lane i's cache
            mine=(); for j in $(seq "$i" "$JN" $((${#JC[@]}-1))); do mine+=("${JC[$j]}"); done
            ws=(); for j in $(seq "$i" "$JN" $((${#JWA[@]}-1))); do ws+=("${JWA[$j]}"); done
            [ ${#mine[@]} -gt 0 ] && [ ${#ws[@]} -gt 0 ] || continue
            "$PYBIN" run/lib/jitwp/sweep_jit_fast.py --stage "$JITWP_DIR/stage" --out "$RUN_OUT/jitfast/jlane$i" \
                --csv "$RUN_OUT/overhead.jlane$i.csv" --suites java --configs "$JSWEEP" --warm-only \
                --warm-slots "${ws[*]}" --java-cells "$(IFS=,; echo "${mine[*]}")" > "$RUN_OUT/jitfast.jwarm$i.log" 2>&1 &
        done
        wait
        for i in $(seq 0 $((JN-1))); do
            mine=(); for j in $(seq "$i" "$JN" $((${#JC[@]}-1))); do mine+=("${JC[$j]}"); done
            [ ${#mine[@]} -gt 0 ] || continue
            parts+=("$RUN_OUT/overhead.jlane$i.csv")
            JAVA_CORES="${JLC[$i]}" JAVA_JIT_CORES="${JLJ[$i]}" "$PYBIN" run/lib/jitwp/sweep_jit_fast.py --stage "$JITWP_DIR/stage" \
                --out "$RUN_OUT/jitfast/jlane$i" --csv "$RUN_OUT/overhead.jlane$i.csv" --suites java --java-reps "$JREPS" --mutex-inherit \
                --configs "$JSWEEP" \
                --java-cells "$(IFS=,; echo "${mine[*]}")" > "$RUN_OUT/jitfast.jlane$i.log" 2>&1 &
        done
        wait
        "$PYBIN" run/lib/merge_csv.py "$CSV" "${parts[@]}"
    fi
fi

# every (suite, configuration) must have valid rows; a silently dropped cell is an error
"$PYBIN" - "$CSV" <<'PYCHK'
import csv, sys
ok, seen = set(), set()
for r in csv.DictReader(open(sys.argv[1])):
    seen.add((r["suite"], r["config"]))
    if r["rc"] == "0" and r["ktime"]:
        ok.add((r["suite"], r["config"]))
bad = sorted(seen - ok)
if bad:
    print("ERROR: no valid rows for " + ", ".join("%s/%s" % b for b in bad) + " (see the err lines above;"
          " the figure shows them as Err; rerun to retry)")
PYCHK
progress_stop
"$PYBIN" figures/six_suite_overhead.py --csv "$CSV" --outdir "$RUN_OUT"
echo "== E1 done: $RUN_OUT/six_suite_overhead.png (Figure 5) =="
