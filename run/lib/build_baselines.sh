#!/usr/bin/env bash
# Build the traditional-tracer baselines of Figure 5 (run once, in the image build; never fails).
#
#   memtrace  third_party/memtrace/p4/obj-intel64/memtrace.so     (Pin 4.4)
#   libdft    third_party/libdft64/tools/obj-intel64/track.so     (Pin 3.20; the Makefile is NOT parallel-safe: -j1)
#   valgrind  third_party/valgrind-build/bin/valgrind             (3.22.0 + valgrind-lackey-memtrace.patch)
#   spindle   third_party/spindle-plus/pass/libSTracerPlusPass.so (LLVM 18 pass; clang-18 builds the targets)
#   spindle_py third_party/spindle-plus/cpython/bin/python3.12{,s} (CPython 3.12.13, plain / through the pass)
#
# A tracer that cannot be built is listed in third_party/BASELINES_ABSENT.txt; run/fig5_traditional.sh
# then records every cell of that tracer as an error row instead of stopping.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TP="$ROOT/third_party"
ABSENT="$TP/BASELINES_ABSENT.txt"
: > "$ABSENT"
absent() { echo "$1 $2" >> "$ABSENT"; echo "WARNING: baseline $1 not built ($2)"; }

echo "== memtrace (Pin 4.4)"
[ -f "$TP/memtrace/p4/obj-intel64/memtrace.so" ] || \
    make -C "$TP/memtrace/p4" PIN_ROOT="$TP/pin-4.4" obj-intel64/memtrace.so > /dev/null 2>&1
[ -f "$TP/memtrace/p4/obj-intel64/memtrace.so" ] || absent memtrace "make failed"

echo "== libdft (Pin 3.20)"
[ -f "$TP/libdft64/tools/obj-intel64/track.so" ] || \
    MAKEFLAGS= make -j1 -C "$TP/libdft64" PIN_ROOT="$TP/pin-3.20" > /dev/null 2>&1
[ -f "$TP/libdft64/tools/obj-intel64/track.so" ] || absent libdft "make failed"

echo "== valgrind 3.22.0 + lackey memtrace patch"
if [ ! -x "$TP/valgrind-build/bin/valgrind" ]; then
    (   set -e
        cd "$TP"
        echo "c811db5add2c5f729944caf47c4e7a65dcaabb9461e472b578765dd7bf6d2d4c  valgrind-3.22.0.tar.bz2" | sha256sum -c --quiet
        rm -rf valgrind-3.22.0
        tar xjf valgrind-3.22.0.tar.bz2
        cd valgrind-3.22.0
        patch -p1 -s < ../valgrind-lackey-memtrace.patch
        ./configure --prefix="$TP/valgrind-build" > /dev/null
        make -j"$(nproc)" > /dev/null 2>&1
        make install > /dev/null 2>&1
        cd .. && rm -rf valgrind-3.22.0
    ) || true
fi
[ -x "$TP/valgrind-build/bin/valgrind" ] || absent valgrind "build failed"

echo "== spindle-plus (LLVM 18 pass)"
if command -v clang-18 > /dev/null && command -v opt-18 > /dev/null && command -v llvm-link-18 > /dev/null; then
    [ -f "$TP/spindle-plus/pass/libSTracerPlusPass.so" ] || \
        ( cd "$TP/spindle-plus/pass" && PATH="/usr/lib/llvm-18/bin:$PATH" bash build.sh > /dev/null 2>&1 ) || true
    [ -f "$TP/spindle-plus/pass/libSTracerPlusPass.so" ] || absent spindle "pass build failed"
    if [ -f "$TP/spindle-plus/pass/libSTracerPlusPass.so" ]; then
        echo "== spindle-plus: CPython 3.12.13 (plain and through the pass)"
        bash "$ROOT/run/lib/build_spindle_cpython.sh" || absent spindle_py "CPython build failed (third_party/spindle-plus/cpython/*.log)"
    else
        absent spindle_py "no pass"
    fi
else
    absent spindle "clang-18/opt-18/llvm-link-18 not installed"
    absent spindle_py "clang-18/opt-18/llvm-link-18 not installed"
fi
echo "== baselines built; absent: $(cut -d' ' -f1 "$ABSENT" | tr '\n' ' ')"
exit 0
