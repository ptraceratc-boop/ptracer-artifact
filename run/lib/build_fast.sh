#!/usr/bin/env bash
# Build the Fast images (static binary rewriting with trampolines, software-buffer sink, the complete plan of
# `analyze.py --mode fast`) that E1 and E3 time. Idempotent: an image that exists is kept.
#
#   bash run/lib/build_fast.sh                                  # all E1 images
#   bash run/lib/build_fast.sh pytree NAME [rewrite.py flags]   # one more CPython tree, e.g. --gt-all
#
# Specs go to $RUN_OUT/build/spec, images to $RUN_OUT/fast (poly/, rust/, mc/, pyfast/) and
# $RUN_OUT/build/NAME for extra trees. A CPython tree is the vanilla install tree with the
# interpreter and its six hot extension modules replaced by their rewritten images.
set -eu
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}"
ANALYZE="$ROOT/ptracer/static/analyze.py"; RW="$ROOT/ptracer/runtime/rewrite.py"
BUILD="$RUN_OUT/build"; SPEC="$BUILD/spec"; FAST="$RUN_OUT/fast"
mkdir -p "$SPEC" "$FAST/poly" "$FAST/rust" "$FAST/mc"
POLY="${POLY_CELLS:-gemm atax jacobi-2d correlation durbin}"
PYMODS="_bisect _json _random _sha2 math zlib"
CPY="$ROOT/suites/pyperformance/cpython-cg"
DYN="lib/python3.12/lib-dynload"

rewrite() {  # <image> <output> [flags]: analyze -> rewrite, routing around unpatchable sites until it converges
    local img="$1" out="$2"; shift 2
    [ -x "$out" ] && return 0
    local n tag; n="$(basename "$img")"; case " $* " in *" --gt-all "*) tag=gt ;; *) tag=fast ;; esac
    local spec="$SPEC/$n.$tag.spec.json" av="$SPEC/$n.$tag.avoid.txt" it
    [ -f "$av" ] || : > "$av"
    for it in 1 2 3 4 5 6 7 8 9 10; do
        if [ ! -s "$spec" ]; then
            local A=""; [ -s "$av" ] && A="--avoid $av"
            "$PYBIN" "$ANALYZE" "$img" --mode fast -o "$spec" $A >&2 || return 1
        fi
        if "$PYBIN" "$RW" "$spec" "$img" -o "$out" --sink buffer "$@" >&2; then return 0; fi
        [ -f "$out.unpatched" ] || return 1
        grep -oE '^0x[0-9a-fA-F]+' "$out.unpatched" >> "$av"; sort -u -o "$av" "$av"
        rm -f "$spec" "$out"
        echo "[build_fast] $n: round $it, $(wc -l < "$av") addresses avoided, re-analyzing" >&2
    done
    echo "[build_fast] $n: did not converge in 10 rounds" >&2; return 1
}
pytree() {  # <dir> [flags]: the CPython tree with interpreter + modules rewritten
    local dir="$1"; shift
    [ -x "$dir/bin/python3.12" ] && [ ! -L "$dir/bin/python3.12" ] && return 0
    rm -rf "$dir"; mkdir -p "$(dirname "$dir")"
    cp -rs "$CPY" "$dir"
    rm -f "$dir/bin/python3.12"
    rewrite "$CPY/bin/python3.12" "$dir/bin/python3.12" "$@"
    for m in $PYMODS; do
        local so="$m.cpython-312-x86_64-linux-gnu.so"
        rm -f "$dir/$DYN/$so"
        rewrite "$CPY/$DYN/$so" "$dir/$DYN/$so" --shared "$@"
    done
}

case "${1:-all}" in
  pytree) shift; name="$1"; shift; pytree "$BUILD/$name" "$@"; echo "$BUILD/$name" ;;
  all)
    for k in $POLY; do rewrite "$ROOT/suites/polybench/targets/${k}_large" "$FAST/poly/${k}_large"; done
    rewrite "$ROOT/suites/rust/micro-bench/target/release/micro-bench" "$FAST/rust/micro-bench"
    rewrite "$ROOT/suites/memcached/bin/memcached_sym" "$FAST/mc/memcached_sym"
    pytree "$FAST/pyfast"
    echo "Fast images ready under $FAST" ;;
  *) echo "usage: $0 [all | pytree NAME [rewrite flags]]"; exit 2 ;;
esac
