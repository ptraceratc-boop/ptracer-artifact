#!/usr/bin/env bash
# E1 -- Figure 5 (runtime overhead): HiFi on all six suites, Fast on PolyBench/C,
# pyperformance, Memcached and Rust, each beside the paper's bar.
#
#   bash run/01_overhead.sh          # needs Intel PT, the Pin 4.4 kit and ptracer/build.sh
#
# HiFi = Intel Pin 4.4 JIT + the HiFi Pintool (-inter_trace_liveness) + software-buffer sink;
# Fast = the binary-rewritten image (trampolines) (run/lib/build_fast.sh) + software-buffer sink; both
# under a complete Intel PT capture whose streams are discarded. Rows already in the CSV are
# not re-measured, so the script can be resumed.
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python
pick_out
CSV="$RUN_OUT/overhead.csv"
REPS="${REPS:-5}"; JREPS="${JREPS:-3}"
cat <<EOF
== E1 runtime overhead: $REPS repetitions per cell (Java $JREPS), configurations alternated ==
Estimated time: 2-4 compute-h for the sweep plus ~1 compute-h to build the Fast images.
Results -> $CSV, figure -> $RUN_OUT/six_suite_overhead.png
EOF
quiet_machine_hints
PIN="${PIN_ROOT:-$ROOT/third_party/pin-4.4}"
[ -x "$PIN/pin" ] || { echo "FATAL: Pin 4.4 kit not found at $PIN (set PIN_ROOT)"; exit 2; }
[ -x "$ROOT/ptracer/offline/pt_capture2" ] || { echo "FATAL: not built (bash ptracer/build.sh)"; exit 2; }

bash run/lib/build_fast.sh

# The Pintool matches an image by absolute path; the plans are stored repo-relative.
PLANS_ABS="$RUN_OUT/build/plans"; mkdir -p "$PLANS_ABS"
for p in "$ROOT"/data/plans/*.plan; do
    sed -E "s#^I (suites/)#I $ROOT/\1#" "$p" > "$PLANS_ABS/$(basename "$p")"
done
JIT_ARGS=()
ADDON="$ROOT/ptracer/runtime/jit/jithook.node"; AGENT="$ROOT/ptracer/runtime/jit/java/ptjava.so"
[ -f "$ADDON" ] && [ -f "$AGENT" ] || { echo "FATAL: JIT hooks not built (bash ptracer/build.sh)"; exit 2; }
JIT_ARGS=(--jit-addon "$ADDON" --jit-agent "$AGENT"
          --jit-cache "$RUN_OUT/build/jitcache" --jit-dir "$RUN_OUT/build/jitdir")
PINX="-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1 -xyzzy -inter_trace_liveness 1"
sweep() {  # <suites> <configs>
    PLANS_DIR="$PLANS_ABS" FAST_DIR="$RUN_OUT/fast" "$PYBIN" run/lib/repslice.py \
        --suites "$1" --configs "$2" --reps "$REPS" --java-reps "$JREPS" \
        --pin-root "$PIN" --pin-extra "$PINX" --label v1 --csv "$CSV" "${JIT_ARGS[@]}"
}
sweep poly,pyperf,rust,mc vanilla,pinhifi,fast
# Node.js cells run only when suites/node is present (shipped separately while it is being updated).
if [ -e "$(dirname "$0")/../suites/node/bin/node" ] || [ -e "$(dirname "$0")/../suites/node/bin/node.part-00" ]; then
    sweep node,java      vanilla,pinhifi,fast
else
    sweep java           vanilla,pinhifi,fast
fi

"$PYBIN" figures/six_suite_overhead.py --csv "$CSV" --outdir "$RUN_OUT"
echo "== E1 done: $RUN_OUT/six_suite_overhead.png (Figure 5) =="
