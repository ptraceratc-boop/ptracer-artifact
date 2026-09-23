#!/usr/bin/env bash
# Build everything the three experiments need, from source, against the vendored kits.
#
#   bash ptracer/build.sh          # build the core tool
#   bash ptracer/build.sh --with-baselines   # also build the optional traditional tracers
#
# Paths are resolved relative to the repository root; override any with an environment
# variable (PIN_ROOT, PIN320_ROOT, E9PATCH_DIR, JDK_HOME, NODE_INCLUDE, PYBIN).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TP="$ROOT/third_party"
SUITES="$ROOT/suites"
PT="$ROOT/ptracer"

E9PATCH_DIR="${E9PATCH_DIR:-$TP/e9patch}"
ZYDIS_DIR="${ZYDIS_DIR:-$E9PATCH_DIR/contrib/zydis}"
PIN_ROOT="${PIN_ROOT:-$TP/pin-4.4}"          # Pin 4.4: the HiFi kit
PIN320_ROOT="${PIN320_ROOT:-$TP/pin-3.20}"   # Pin 3.20: only the optional libdft baseline
JDK_HOME="${JDK_HOME:-$SUITES/java/jdk17}"
NODE_INCLUDE="${NODE_INCLUDE:-$SUITES/node/include/node}"
PYBIN="${PYBIN:-python3}"
export E9PATCH_DIR ZYDIS_DIR JDK_HOME NODE_INCLUDE PYBIN

say() { printf '\n=== %s ===\n' "$*"; }
have() { [ -e "$1" ]; }

[ -x "$E9PATCH_DIR/e9tool" ] || { echo "FATAL: E9Patch not found at $E9PATCH_DIR (set E9PATCH_DIR)"; exit 2; }
[ -f "$ZYDIS_DIR/libZydis.a" ] || { echo "FATAL: Zydis static lib not found at $ZYDIS_DIR/libZydis.a"; exit 2; }
"$PYBIN" -c "import pyvex" 2>/dev/null || { echo "FATAL: $PYBIN cannot import pyvex (activate the venv from README step 2 or set PYBIN=<venv>/bin/python)"; exit 2; }

say "offline stage (ptrecon, pt_capture2, ptpktscan)"
make -C "$PT/offline" PY="$PYBIN" ZYDIS="$ZYDIS_DIR"

say "E9Patch plugin + buffer/PTWRITE runtimes"
make -C "$PT/runtime/e9plugin" E9="$E9PATCH_DIR"

if [ -x "$PIN_ROOT/pin" ]; then
    say "Pin 4.4 HiFi Pintool + no-op floor"
    make -C "$PT/runtime/pinjit" PIN_ROOT="$PIN_ROOT" \
        obj-intel64/nooptool.so obj-intel64/hifitool.so
else
    echo "WARNING: Pin 4.4 kit not at $PIN_ROOT -- HiFi overhead rows cannot be re-measured"
fi

say "JIT runtime hooks (V8 addon, HotSpot JVMTI agent)"
if have "$NODE_INCLUDE"; then
    ( cd "$PT/runtime/jit" && ZYDIS_DIR="$ZYDIS_DIR" NODE_INCLUDE="$NODE_INCLUDE" bash build.sh )
else
    echo "WARNING: node headers not at $NODE_INCLUDE -- the Node HiFi hook is not built"
fi
if have "$JDK_HOME/include"; then
    ( cd "$PT/runtime/jit/java" && ZYDIS_DIR="$ZYDIS_DIR" JDK_HOME="$JDK_HOME" bash build.sh )
else
    echo "WARNING: JDK headers not at $JDK_HOME/include -- the Java HiFi agent is not built"
fi

if [ "${1:-}" = "--with-baselines" ]; then
    say "optional baselines (memtrace on Pin 4.4, libdft on Pin 3.20)"
    if [ -x "$PIN_ROOT/pin" ] && have "$TP/memtrace/p4"; then
        make -C "$TP/memtrace/p4" PIN_ROOT="$PIN_ROOT" obj-intel64/memtrace.so || \
            echo "WARNING: memtrace (Pin 4.4) build failed"
    fi
    if [ -x "$PIN320_ROOT/pin" ] && have "$TP/libdft64"; then
        make -C "$TP/libdft64" PIN_ROOT="$PIN320_ROOT" || \
            echo "WARNING: libdft (Pin 3.20) build failed"
    fi
fi

say "build complete"
