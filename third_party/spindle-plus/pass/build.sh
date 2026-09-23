#!/usr/bin/env bash
# Build the Spindle-Plus pass plugin (libSTracerPlusPass.so). Same analysis as the
# base Spindle S-Tracer (MTS.cpp, copied unchanged); pass name = "stracerplus".
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-18}"
CXXFLAGS="$($LLVM_CONFIG --cxxflags) -fPIC -fno-rtti -I$HERE"
echo "[build] clang++ $($LLVM_CONFIG --version)"
clang++ $CXXFLAGS -shared "$HERE/STracer.cpp" "$HERE/MTS.cpp" \
  -o "$HERE/libSTracerPlusPass.so"
echo "[build] wrote $HERE/libSTracerPlusPass.so"
