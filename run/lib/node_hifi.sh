#!/usr/bin/env bash
# Node.js (Web Tooling, 18 benchmarks) HiFi and HiFi-PTWRITE sweep, appended to a Figure 5 CSV.
#
#   bash run/lib/node_hifi.sh <csv>
#
# HiFi = Intel Pin 4.4 + the HiFi Pintool over the WHOLE process: the ELF plan of node and its native images
# (run/lib/node_hifi_plan.sh) plus the sites of V8-generated code, which the V8 hook (jithook.node) analyzes
# asynchronously (PTJIT_ASYNC/PTJIT_PIN_ASYNC, PTJIT_WORKERS analyzer workers) and publishes to the Pintool through the
# Pin bridge.  HiFi-PTWRITE = the same with the Pintool's mixed PTWRITE sink (repslice.py HIFI_PTW: per-site budget of 5 M values/s
# from an untimed count profile of the cell; JIT sites on the buffer).  PT on,
# streams discarded.  Every arm runs node with the same V8 flags and the drain-aware harness (run/lib/jitwp/wtb.js):
# 5 untimed warm-up iterations, an untimed wait (<= WTB_DRAIN_MS) for pending analyses, then 10 timed iterations.
# Rows: suite node, configs vanilla / pinhifi / pinhifi_ptw, label v1 (the figure pairs them per cell).
#
# Knobs: JIT_REPS (3), NODE_HIFI_CONFIGS (vanilla,pinhifi,pinhifi_ptw), NODE_HIFI_CELLS (all 18, comma list),
# TIMED_CORE / HELPER_CORES (as run/01_overhead.sh), NODE_JIT_CORES (16,17,18,19: analyzer services),
# NODE_HIFI_TIMEOUT (3600 s per run).
set -eu
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
CSV="${1:?usage: node_hifi.sh <csv>}"
"$PYBIN" run/lib/row_versions.py prune "$(dirname "$CSV")"
PIN="${PIN_ROOT:-$ROOT/third_party/pin-4.4}"
ADDON="$ROOT/ptracer/runtime/jit/jithook.node"; AGENT="$ROOT/ptracer/runtime/jit/java/ptjava.so"
[ -f "$ADDON" ] || { echo "FATAL: $ADDON not built (bash ptracer/build.sh)"; exit 2; }
[ -e "$ROOT/suites/node/bin/node" ] || { echo "FATAL: suites/node not present"; exit 2; }
PLAN=$(bash run/lib/node_hifi_plan.sh | tail -1)
export PTJ_HIFI_PLAN_NODE="$PLAN"
export PTJIT_ASYNC=1 PTJIT_PIN_ASYNC=1 PTJIT_WORKERS=4 PTJ_PASS_PTJIT=PTJIT_ASYNC,PTJIT_PIN_ASYNC,PTJIT_WORKERS
export PTJ_NODE_V8FLAGS="${PTJ_NODE_V8FLAGS:---no-short-builtin-calls --max-semi-space-size=128 --single-threaded-gc}"
export WTB_JS="$ROOT/run/lib/jitwp/wtb.js" WTB_WARMUP="${WTB_WARMUP:-5}" WTB_ITERS="${WTB_ITERS:-10}" WTB_DRAIN_MS="${WTB_DRAIN_MS:-120000}"
PINX="-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 -xyzzy -inter_trace_liveness 1 -spillslot_memop 0 -assert_on_memory_conflict FIXED_ONLY"
CELLS="${NODE_HIFI_CELLS:-acorn,babel,babel-minify,babylon,buble,chai,coffeescript,espree,esprima,jshint,lebab,postcss,prepack,prettier,source-map,terser,typescript,uglify-js}"
JD="$RUN_OUT/build/nodehifi"; mkdir -p "$JD"
echo "== Node.js HiFi: $CELLS x ${NODE_HIFI_CONFIGS:-vanilla,pinhifi,pinhifi_ptw} x ${JIT_REPS:-3} reps (plan $PLAN) =="
"$PYBIN" run/lib/repslice.py --suites node --cells "$CELLS" \
    --configs "${NODE_HIFI_CONFIGS:-vanilla,pinhifi,pinhifi_ptw}" --reps "${JIT_REPS:-3}" \
    --pin-root "$PIN" --pin-extra "$PINX" --jit-addon "$ADDON" --jit-agent "$AGENT" \
    --jit-cache "$JD/jitcache" --jit-dir "$JD/jitdir" --jit-workers 4 --jit-cores "${NODE_JIT_CORES:-16,17,18,19}" \
    --timeout "${NODE_HIFI_TIMEOUT:-3600}" --label v1 --csv "$CSV"
