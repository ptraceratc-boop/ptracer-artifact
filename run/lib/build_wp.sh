#!/usr/bin/env bash
# Whole-process Fast images for Figure 5 (PolyBench/C, Rust Stream, Memcached, pyperformance): the main image AND
# every ELF image the process maps are rewritten (static binary rewriting, trampolines, software-buffer sink, the
# complete plan of `analyze.py --mode fast`), including the program loader:
#   * rewrite.py --cfr (E9Patch control-flow recovery; the rewriter turns it off for images with C++ exception
#     tables and for the loader), with the E9Patch of third_party/e9patch (PATCHES.md);
#   * ld.so: `--ld-so --under-ld-so --ldfix ldfix.e9rt`, no keyframes, one image for every suite;
#   * every main image names the rewritten loader as its program interpreter (set_interp.py: PT_INTERP ->
#     /ptld/fast/ld.so, a symlink to $FAST/ptld that run/01_overhead.sh creates), so the program starts as usual;
#   * every image except ld.so carries keyframes: `analyze.py --keyframe $FAST_KEYFRAME' (default 128).
# Idempotent (an image that exists is kept).  Output: $RUN_OUT/fastwp/{ptld/fast/ld.so, lib, poly, rust, mc, py}.
#   bash run/lib/build_wp.sh [TARGET...]       TARGET in: ld libs poly rust mc py interp  (default: all)
# Env: POLY_CELLS (default: all 30 kernels), JOBS (analyzer processes, default nproc), ROUNDS (converge rounds).
set -u
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}"
ANALYZE="$ROOT/ptracer/static/analyze.py"; RW="$ROOT/ptracer/runtime/rewrite.py"
FAST="$RUN_OUT/fastwp"; SPECD="$RUN_OUT/build/spec_wp"; CACHE="$RUN_OUT/build/cache_wp"
JOBS="${JOBS:-$(nproc)}"
POLY_ALL="2mm 3mm adi atax bicg cholesky correlation covariance deriche doitgen durbin fdtd-2d floyd-warshall gemm gemver gesummv gramschmidt heat-3d jacobi-1d jacobi-2d lu ludcmp mvt nussinov seidel-2d symm syr2k syrk trisolv trmm"
POLY="${POLY_CELLS:-$POLY_ALL}"
LIBS="/lib/x86_64-linux-gnu/libc.so.6 /lib/x86_64-linux-gnu/libm.so.6 /lib/x86_64-linux-gnu/libgcc_s.so.1 /lib/x86_64-linux-gnu/libevent-2.1.so.7"
LD="$(readlink -f /lib64/ld-linux-x86-64.so.2)"
LDFIX="$ROOT/ptracer/runtime/rt/ldfix.e9rt"
PYD="$ROOT/suites/pyperformance"; CPY="$PYD/cpython-cg"
INTERP=/ptld/fast/ld.so
stale_build "$FAST" "$SPECD"
mkdir -p "$SPECD/nokf" "$SPECD/kf" "$CACHE" "$FAST/ptld/fast" "$FAST/lib" "$FAST/poly" "$FAST/rust" "$FAST/mc" "$FAST/py/lib"
fail=0

unpatched_from_reloc() {  # <output>: spec sites ($out.sites) that have no `I' (patched) row in $out.e9reloc
    [ -s "$1.sites" ] && [ -s "$1.e9reloc" ] || return 1
    "$PYBIN" - "$1.sites" "$1.e9reloc" <<'PY'
import sys
sites = [int(l.split()[0], 16) for l in open(sys.argv[1]) if l.startswith("0x")]
patched = set()
for l in open(sys.argv[2]):
    f = l.split()
    if len(f) >= 4 and not l.startswith("#") and f[3] == "I":
        patched.add(int(f[0], 16))
for a in sites:
    if a not in patched:
        print(hex(a))
PY
}

rewrite() {  # a failed image must not survive: the idempotence check below would take it for a finished one
    rewrite_one "$@" && return 0
    [ -e "$2" ] && mv -f "$2" "$2.failed"
    return 1
}
rewrite_one() {  # <image> <output> <nokf|kf> [rewrite.py flags]: analyze -> rewrite, routing around unpatchable sites
    local img="$1" out="$2" kfd="$3"; shift 3
    [ -x "$out" ] && [ ! -L "$out" ] && return 0
    rm -f "$out"
    local KF=""; [ "$kfd" = kf ] && KF="--keyframe $FAST_KEYFRAME"
    local n; n="$(echo "$img" | md5sum | cut -c1-6)_$(basename "$img")"
    local spec="$SPECD/$kfd/$n.spec.json" av="$SPECD/$kfd/$n.avoid.txt" it t0; t0=$(date +%s)
    [ -f "$av" ] || : > "$av"
    for it in $(seq 1 "${ROUNDS:-40}"); do
        if [ ! -s "$spec" ]; then
            local A=""; [ -s "$av" ] && A="--avoid $av"
            "$PYBIN" "$ANALYZE" "$img" --mode fast $KF --jobs "$JOBS" --cache "$CACHE" -o "$spec" $A >&2 || return 1
        fi
        if "$PYBIN" "$RW" "$spec" "$img" -o "$out" --sink buffer --cfr $(carrier_args "$@") "$@" >&2; then
            echo "[build_wp] $n: OK round $it, $(( $(date +%s) - t0 )) s" >&2; return 0; fi
        # A site the rewriter cannot map exactly (listed in <out>.sitemap.problems, e.g. an E9Patch trampoline whose
        # instruction does not match the original) is routed around like an unpatched one: avoided and re-analyzed.
        if [ ! -f "$out.unpatched" ] && grep -qE '^0x[0-9a-fA-F]+' "$out.sitemap.problems" 2>/dev/null; then
            grep -oE '^0x[0-9a-fA-F]+' "$out.sitemap.problems" > "$out.unpatched"
            echo "[build_wp] $n: site-map problem(s) at $(tr '\n' ' ' < "$out.unpatched")-> avoided" >&2
        fi
        [ -f "$out.unpatched" ] || return 1
        if ! grep -qE '^0x[0-9a-fA-F]+' "$out.unpatched"; then
            # e9tool patched fewer sites than asked but the rewriter's own scan names none (e.g. a patch
            # jump folded into a neighbour's trampoline): take the sites from a --keep-going run's problem list.
            # First source: E9Patch's own relocation record (E9PATCH_RELOCMAP, $out.e9reloc) has an `I' row for every
            # instruction it actually patched; a spec site without one is unpatched even when the CFR scan maps it
            # (e.g. a 1-byte NOP jump-table target).  Second source: a
            # --keep-going run's problem list.
            unpatched_from_reloc "$out" > "$out.unpatched.reloc" && [ -s "$out.unpatched.reloc" ] \
                && cp "$out.unpatched.reloc" "$out.unpatched"
            if ! grep -qE '^0x[0-9a-fA-F]+' "$out.unpatched"; then
                "$PYBIN" "$RW" "$spec" "$img" -o "$out.kg" --sink buffer --cfr $(carrier_args "$@") "$@" --keep-going > /dev/null 2>&1 || true
                grep -oE '^0x[0-9a-fA-F]+' "$out.kg.sitemap.problems" > "$out.unpatched" 2>/dev/null
                rm -f "$out.kg" "$out.kg".*
            fi
            if ! grep -qE '^0x[0-9a-fA-F]+' "$out.unpatched"; then
                echo "[build_wp] $n: e9tool patched fewer sites than asked and no source names the missing one" >&2
                return 1; fi
            echo "[build_wp] $n: unreported unpatched site(s): $(grep -oE '^0x[0-9a-fA-F]+' "$out.unpatched" | tr '\n' ' ')" >&2
        fi
        grep -oE '^0x[0-9a-fA-F]+' "$out.unpatched" >> "$av"; sort -u -o "$av" "$av"
        rm -f "$spec" "$out"
        echo "[build_wp] $n: round $it, $(wc -l < "$av") addresses avoided, re-analyzing" >&2
    done
    echo "[build_wp] $n: did not converge in ${ROUNDS:-40} rounds" >&2; return 1
}
keepgoing() {  # <image> <output> <avoid seed>: libcrypto / libffi never converge (e9tool matches a constant set of
    # instructions it cannot patch); they start from the recorded avoid list and are built once with --keep-going.
    # The remaining unpatched sites are listed in <output>.unpatched and reported (they count as Fast loss).
    local img="$1" out="$2" seed="$3"
    [ -x "$out" ] && [ ! -L "$out" ] && return 0
    rm -f "$out"
    local n; n="$(echo "$img" | md5sum | cut -c1-6)_$(basename "$img")"
    local spec="$SPECD/kf/$n.spec.json" av="$SPECD/kf/$n.avoid.txt"
    [ -s "$av" ] || cp "$seed" "$av"
    [ -s "$spec" ] || "$PYBIN" "$ANALYZE" "$img" --mode fast --keyframe "$FAST_KEYFRAME" --jobs "$JOBS" --cache "$CACHE" -o "$spec" --avoid "$av" >&2 || return 1
    "$PYBIN" "$RW" "$spec" "$img" -o "$out" --sink buffer --cfr --shared --keep-going >&2 || return 1
    echo "[build_wp] KEEP-GOING $(basename "$out"): $(grep -c '^0x' "$out.unpatched" 2>/dev/null || echo 0) sites unpatched, $(wc -l < "$av") avoided" >&2
}
pyout() {  # <real path> <loader name> -> output path inside $FAST/py
    case "$1" in
      "$CPY"/*)            echo "$FAST/py/pyfast/${1#$CPY/}" ;;
      "$PYD"/wp/site/*)    echo "$FAST/py/pysite/${1#$PYD/wp/site/}" ;;
      "$PYD"/wp/extmods/*) echo "$FAST/py/pyext/${1#$PYD/wp/extmods/}" ;;
      *)                   echo "$FAST/py/lib/$(basename "$2")" ;;
    esac
}
pyimages() { grep -v '^#' "$PYD/wp/images.tsv" | sed "s#@PY@#$PYD#g"; }

for t in ${*:-ld libs poly rust mc py interp}; do case $t in
  ld)   echo "[build_wp] loader $LD sha256 $(sha256sum "$LD" | cut -c1-16)" >&2
        rewrite "$LD" "$FAST/ptld/fast/ld.so" nokf --ld-so --under-ld-so --ldfix "$LDFIX" || fail=1 ;;
  libs) for l in $LIBS; do rewrite "$(readlink -f "$l")" "$FAST/lib/$(basename "$l")" kf --shared || fail=1; done ;;
  poly) for k in $POLY; do rewrite "$ROOT/suites/polybench/targets/${k}_large" "$FAST/poly/${k}_large" kf || fail=1; done ;;
  rust) rewrite "$ROOT/suites/rust/micro-bench/target/release/micro-bench" "$FAST/rust/micro-bench" kf || fail=1 ;;
  mc)   rewrite "$ROOT/suites/memcached/bin/memcached_sym" "$FAST/mc/memcached_sym" kf || fail=1 ;;
  py)   bash "$PYD/wp/setup_site.sh" --check-only || exit 3
        [ -d "$FAST/py/pyfast" ] || cp -rs "$CPY" "$FAST/py/pyfast"
        [ -d "$FAST/py/pysite" ] || cp -rs "$PYD/wp/site" "$FAST/py/pysite"
        [ -d "$FAST/py/pyext" ] || cp -rs "$PYD/wp/extmods" "$FAST/py/pyext"
        while IFS=$'\t' read -r real ld sha size; do
            out=$(pyout "$real" "$ld")
            case "$real" in
              */libcrypto.so.3)    keepgoing "$real" "$out" "$ROOT/data/avoid/libcrypto.so.3.avoid.txt" || fail=1 ;;
              */libffi.so.8.1.4)   keepgoing "$real" "$out" "$ROOT/data/avoid/libffi.so.8.1.4.avoid.txt" || fail=1 ;;
              */bin/python3.12)    rewrite "$real" "$out" kf || fail=1 ;;
              *)                   rewrite "$real" "$out" kf --shared || fail=1 ;;
            esac
        done < <(pyimages) ;;
  interp) for f in "$FAST"/poly/*_large "$FAST/rust/micro-bench" "$FAST/mc/memcached_sym" "$FAST/py/pyfast/bin/python3.12"; do
            [ -f "$f" ] && [ ! -L "$f" ] && { "$PYBIN" run/lib/set_interp.py "$f" "$INTERP" >&2 || fail=1; }
        done ;;
  *) echo "usage: $0 [ld libs poly rust mc py interp]"; exit 2 ;;
esac; done
[ $fail = 0 ] || { echo "[build_wp] INCOMPLETE (see the messages above)"; exit 1; }
echo "[build_wp] whole-process Fast images ready under $FAST"
