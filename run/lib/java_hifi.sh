#!/usr/bin/env bash
# Java (Renaissance, 25 benchmarks) HiFi and HiFi-PTWRITE sweep, appended to a Figure 5 CSV.
#
#   bash run/lib/java_hifi.sh <csv>
#
# HiFi = Intel Pin 4.4 + the HiFi Pintool over the WHOLE process: the ELF plan of the JVM's native images
# (run/lib/java_hifi_plan.sh) plus the sites of HotSpot-generated code, which the JVMTI agent (ptjava.so, pin=1)
# analyzes on its analyzer services and publishes to the Pintool through the Pin bridge.  HiFi-PTWRITE = the same with
# the Pintool's mixed PTWRITE sink (repslice.py HIFI_PTW: per-site budget of 5 M values/s from an untimed count profile of
# the cell; JIT sites on the buffer).  PT on, streams discarded.  Every arm runs the JVM with
# -XX:UseAVX=2 and netty's library path, and the ptjdrain boundary drain: an untimed wait (<= JAVA_DRAIN_MS) for pending
# analyses before the first timed iteration.  Steady state = median of the iterations after the warm-up drop.
# Rows: suite java, configs vanilla / pinhifi / pinhifi_ptw, label v1.
#
# Requires kernel.yama.ptrace_scope=0 on the host: under Pin 4.4 a JVM that starts a child process (Spark and Hadoop
# run chmod/rm) needs Pin's server to ptrace-attach to the exec-ing child; with ptrace_scope=1 the exec fails.
#
# Knobs: JREPS (1), JAVA_HIFI_CONFIGS (vanilla,pinhifi,pinhifi_ptw), JAVA_HIFI_CELLS (all 25, comma list),
# JAVA_HIFI_ITERS / JAVA_HIFI_DROP (5 / 2), JAVA_LANES / JAVA_LANE_CORES / JAVA_LANE_JIT (as the Java Fast sweep),
# JAVA_LANE_HELPER (PT capture cores per lane: "4-5 36-37"), JAVA_HIFI_TIMEOUT (3600 s per run, plus the JAVA_DRAIN_MS bound).  The lanes take
# benchmarks from one shared queue.
set -eu
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
CSV="${1:?usage: java_hifi.sh <csv>}"
"$PYBIN" run/lib/row_versions.py prune "$(dirname "$CSV")"
PIN="${PIN_ROOT:-$ROOT/third_party/pin-4.4}"
AGENT="$ROOT/ptracer/runtime/jit/java/ptjava.so"; ADDON="$ROOT/ptracer/runtime/jit/jithook.node"
[ -f "$AGENT" ] || { echo "FATAL: $AGENT not built (bash ptracer/build.sh)"; exit 2; }
[ -f "$ROOT/suites/java/renaissance/renaissance.jar" ] || { echo "FATAL: suites/java not present"; exit 2; }
if [ "$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)" != 0 ]; then
    echo "ERROR: kernel.yama.ptrace_scope must be 0 for the Java Pin arms (on the host: sudo sysctl -w kernel.yama.ptrace_scope=0)"
    exit 2
fi
PLAN=$(bash run/lib/java_hifi_plan.sh | tail -1)
NETTY="$RUN_OUT/build/java_hifi/netty"
CELLS="${JAVA_HIFI_CELLS:-akka-uct,als,chi-square,db-shootout,dec-tree,dotty,finagle-chirper,finagle-http,fj-kmeans,future-genetic,gauss-mix,log-regression,mnemonics,movie-lens,naive-bayes,neo4j-analytics,page-rank,par-mnemonics,philosophers,reactors,rx-scrabble,scala-doku,scala-kmeans,scala-stm-bench7,scrabble}"
export PTJ_HIFI_PLAN_JAVA="$PLAN" PTJ_JAVA_BENCH="$CELLS" PTJ_JAVA_ITERS_ALL=1 PTJ_FAST=0
export PTJ_JAVA_XOPTS="${JAVA_XFLAGS:--XX:UseAVX=2} -Djava.library.path=$NETTY" PTJ_DRAIN_MS="${JAVA_DRAIN_MS:-1800000}"
# per-run limit: JAVA_HIFI_TIMEOUT plus the drain bound, so a full drain wait never turns a valid row into a timeout
TMO=$(( ${JAVA_HIFI_TIMEOUT:-3600} + ${JAVA_DRAIN_MS:-1800000} / 1000 ))
PINX="-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 -xyzzy -inter_trace_liveness 1 -spillslot_memop 0 -assert_on_memory_conflict FIXED_ONLY"
JD="$RUN_OUT/build/javahifi"; mkdir -p "$JD"
read -r -a JLC <<< "${JAVA_LANE_CORES:-0-3 24-27}"; read -r -a JLJ <<< "${JAVA_LANE_JIT:-8,9,10,11,16,17,18,19 28,29,30,31,32,33,34,35}"
read -r -a JLH <<< "${JAVA_LANE_HELPER:-4-5 36-37}"
JN="${JAVA_LANES:-2}"; [ "$JN" -le "${#JLC[@]}" ] || JN=${#JLC[@]}
read -r -a JC <<< "$(echo "$CELLS" | tr ',' ' ')"
echo "== Java HiFi: ${#JC[@]} cells x ${JAVA_HIFI_CONFIGS:-vanilla,pinhifi,pinhifi_ptw} x ${JREPS:-1} reps, $JN lanes (plan $PLAN) =="
# Lanes share one work queue: a free lane takes the next benchmark (one CSV per benchmark, so a resumed run skips
# the rows it has whichever lane measured them).
Q="$JD/queue"; printf '%s\n' "${JC[@]}" > "$Q"
next_cell() { flock "$Q.lock" sh -c 'c=$(head -n 1 "$1"); [ -n "$c" ] || exit 1; sed -i 1d "$1"; echo "$c"' _ "$Q"; }
mkdir -p "$RUN_OUT/jhifi"
lane() {
    local i="$1" c
    while c=$(next_cell); do
        JAVA_CORES="${JLC[$i]}" HELPER_CORES="${JLH[$i]}" "$PYBIN" run/lib/repslice.py --suites java \
            --cells "$c" --configs "${JAVA_HIFI_CONFIGS:-vanilla,pinhifi,pinhifi_ptw}" \
            --java-reps "${JREPS:-1}" --java-pin-iters "${JAVA_HIFI_ITERS:-5}" --java-pin-drop "${JAVA_HIFI_DROP:-2}" \
            --pin-root "$PIN" --pin-extra "$PINX" --hifi-extra "-pages 2048" --jit-addon "$ADDON" --jit-agent "$AGENT" \
            --jit-cache "$JD/jitcache.$i" --jit-dir "$JD/jitdir.$i" --jit-workers 8 --jit-cores "${JLJ[$i]}" \
            --mutex-inherit --timeout "$TMO" --label v1 --csv "$RUN_OUT/jhifi/$c.csv" \
            >> "$RUN_OUT/javahifi.lane$i.log" 2>&1 || true
    done
}
for i in $(seq 0 $((JN-1))); do lane "$i" & done
wait_jobs
parts=(); for c in "${JC[@]}"; do parts+=("$RUN_OUT/jhifi/$c.csv"); done
"$PYBIN" run/lib/merge_csv.py "$CSV" "${parts[@]}"
