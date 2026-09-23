#!/usr/bin/env bash
# OPTIONAL / SLOW -- the paper's traditional-tracer baselines over the six-suite slice.
#
#   bash run/optional_baselines.sh [memtrace|libdft|valgrind|spindle] ...
#
# These are the reference points PTracer is compared against, NOT part of the three main
# experiments. They are slow (hours; slowdown capped at 200x per the paper) and each needs a
# different tool built. Run one at a time; results append to run/lib/out/baselines.csv.
#
#   memtrace  memorytracer: Pin fill-buffer over every operand (Pin 4.4 kit)
#   libdft    byte-level taint tracking (Pintool; libdft only builds on the Pin 3.20 kit,
#             so its Pin floor differs from memtrace/HiFi -- do not subtract one from the other)
#   valgrind  the Valgrind lackey baseline (apt-get install valgrind, or set VALGRIND=/path)
#   spindle   Spindle-plus, the paper's LLVM static-analysis tracer -- PolyBench + pyperformance
#             only. NOT vendored: it needs an LLVM pass build; see the note printed below.
set -eu
. "$(dirname "$0")/lib/common.sh"
cd "$ROOT"
pick_python

WHICH="${*:-memtrace libdft valgrind}"
CSV="${BASELINES_CSV:-$ROOT/run/lib/out/baselines.csv}"
pick_out
CAP_REF="${CAP_REF:-$RUN_OUT/overhead.csv}"   # the vanilla walls of E1 give the 200x budget
[ -f "$CAP_REF" ] || { echo "FATAL: $CAP_REF not found: run run/01_overhead.sh (or run/experiments_v1.sh) first"; exit 2; }
mkdir -p "$(dirname "$CSV")"
echo "== OPTIONAL baselines: $WHICH (SLOW; 200x cap) =="
quiet_machine_hints

PIN4="${PIN_ROOT:-$ROOT/third_party/pin-4.4}"
PIN320="${PIN320_ROOT:-$ROOT/third_party/pin-3.20}"
PINX="-pin_memory_range 0x1000000000:0x1400000000 -enforce_pin_range_allocations 1"

run_tracer() {  # <config> <extra repslice args...>
    local cfg="$1"; shift
    "$PYBIN" run/lib/repslice.py --configs "vanilla,$cfg" \
        --suites poly,pyperf,mc,rust,node,java --reps "${REPS:-5}" --java-reps "${JREPS:-3}" \
        --cap-x 200 --cap-from "$CAP_REF" --stop-after-fail \
        --csv "$CSV" --label "$cfg" "$@"
}

for w in $WHICH; do
    case "$w" in
    memtrace)
        [ -x "$PIN4/pin" ] || { echo "skip memtrace: Pin 4.4 kit missing"; continue; }
        echo "-- memtrace (Pin 4.4) --"
        run_tracer memtrace --pin-root "$PIN4" --pin-extra "$PINX" \
            --memtrace-tool "$ROOT/third_party/memtrace/p4/obj-intel64/memtrace.so" ;;
    libdft)
        [ -x "$PIN320/pin" ] || { echo "skip libdft: Pin 3.20 kit missing (third_party/pin-3.20)"; continue; }
        echo "-- libdft (Pin 3.20) --"
        run_tracer libdft --libdft-pin-root "$PIN320" \
            --libdft-tool "$ROOT/third_party/libdft64/tools/obj-intel64/track.so" ;;
    valgrind)
        command -v "${VALGRIND:-valgrind}" >/dev/null 2>&1 || {
            echo "skip valgrind: not installed (apt-get install valgrind, or set VALGRIND=)"; continue; }
        echo "-- valgrind --"
        run_tracer valgrind --valgrind "${VALGRIND:-valgrind}" ;;
    spindle)
        cat <<'EOF'
-- spindle-plus: NOT vendored --
Spindle-plus is a compile-time tracer (an LLVM pass + a runtime library). It cannot instrument
the prebuilt suite binaries; it needs its own clang -O2 build of PolyBench and CPython as the
denominator (which is how the paper defines its Spindle-plus column). To reproduce it: build
the LLVM pass and runtime from the Spindle-plus sources, compile the PolyBench sources under
suites/polybench/polybench-c and a CPython with the pass, and time both with the benchmark's
own timer. It has no Node/Java/Memcached cell by construction.
EOF
        ;;
    *) echo "unknown baseline: $w" ;;
    esac
done
echo "== done. Table:  python3 -c 'import csv; ...'  or inspect $CSV =="
