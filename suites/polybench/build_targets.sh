#!/bin/bash
# Rebuild the PolyBench/C 4.2.1 kernels as <kernel>_{mini,small,large} into targets/.
# The kernel_* function is kept out of line so the analyzer sees a clean function boundary.
# The vendored targets/ are the images the cached specs are keyed to; a rebuild with another
# compiler produces different bytes and needs a fresh analysis.
set -u
cd "$(dirname "$0")"
PB=./polybench-c
OUT=${1:-./targets}
mkdir -p "$OUT"
n=0
while read -r src; do
  name=$(basename "$src" .c); name=${name,,}
  kdir=$(dirname "$src")
  for DS in MINI SMALL LARGE; do
    gcc -O1 -fno-inline -fno-inline-functions-called-once -g \
      -I "$PB/utilities" -I "$kdir" "$PB/utilities/polybench.c" "$src" \
      -DPOLYBENCH_USE_C99_PROTO -D${DS}_DATASET -DPOLYBENCH_TIME -lm \
      -o "$OUT/${name}_${DS,,}" || echo "FAILED $name $DS"
  done
  n=$((n+1))
done < <(find "$PB" -name '*.c' ! -name 'polybench.c' ! -name 'template*' ! -name '*.orig.c' | sort)
echo "built $n kernels x 3 sizes into $OUT"
