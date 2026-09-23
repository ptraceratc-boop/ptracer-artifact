#!/usr/bin/env bash
# Build PolyBench/C kernels into ./bin/ with a chosen dataset size.
#
#   ./build.sh [DATASET] [kernel ...]
#
# DATASET in {MINI,SMALL,MEDIUM,LARGE,EXTRALARGE} (default: MEDIUM).
# With no kernel names, builds every kernel in the suite.
#
# Each kernel is built two ways:
#   bin/<name>            -DPOLYBENCH_TIME           (prints kernel time to stdout)
#   bin/<name>.dump       -DPOLYBENCH_DUMP_ARRAYS    (dumps result arrays to stderr;
#                                                     used for output-divergence checks)
# The .dump variant lets us compare a tracer's result against vanilla deterministically.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASET="${1:-MEDIUM}"; shift || true
OUT="$ROOT/bin"; mkdir -p "$OUT"
UTIL="$ROOT/utilities"

mapfile -t ALL < <(find "$ROOT" -name '*.c' \
    ! -name 'polybench.c' ! -name 'template*' ! -name '*.orig.c' | sort)

want=("$@")
built=0
for src in "${ALL[@]}"; do
    name="$(basename "$src" .c)"
    name="${name,,}"                      # lowercase (Nussinov.c -> nussinov)
    if [[ ${#want[@]} -gt 0 ]]; then
        match=0; for w in "${want[@]}"; do [[ "$name" == "${w,,}" ]] && match=1; done
        [[ $match -eq 0 ]] && continue
    fi
    kdir="$(dirname "$src")"
    common=(-O2 -I "$UTIL" -I "$kdir" "$UTIL/polybench.c" "$src" -DPOLYBENCH_USE_C99_PROTO -D"${DATASET}_DATASET" -lm)
    gcc "${common[@]}" -DPOLYBENCH_TIME        -o "$OUT/$name"        && \
    gcc "${common[@]}" -DPOLYBENCH_DUMP_ARRAYS -o "$OUT/$name.dump"   && \
        { echo "built $name"; built=$((built+1)); } || echo "FAILED $name"
done
echo "== built $built kernels into $OUT (dataset=$DATASET) =="
