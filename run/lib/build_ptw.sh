#!/usr/bin/env bash
# Fast-PTWRITE images for Figure 5 (PolyBench/C, Rust Stream, Memcached, pyperformance): the whole-process Fast tree of
# run/lib/build_wp.sh with the MIXED sink in the images that carry a PTWRITE site list (data/ptw_sites): the listed
# sites log through Intel PT `ptwrite', every other site through the software buffer, exactly as in Fast.
#   * The lists are the per-site PTWRITE budget of 5 M values/s (sites ordered by their execution rate on the suites'
#     benchmarks, PTWRITE while every benchmark stays under the budget); a list is `ALL' when every site fits.
#     They name a site by address, `when' and a hash of its spec entry, so they apply to the specs build_wp.sh made;
#     a site the local spec does not have in the same form is logged through the buffer.
#   * Built from the SAME specs as the Fast images, with --cfr, by the reference rewriter that has the mixed sink
#     (ptracer/runtime/jit_toolchain/rewrite_cfr.py, its e9tool plugin and injected runtime); consecutive PTWRITEs of
#     a trampoline are spaced by PTW_SPACE (default 12) filler instructions so the PT packet buffer keeps up.
#   * Every image without a list, or whose mixed build fails, is the Fast image itself (a symlink into fastwp), so
#     Fast and Fast-PTWRITE differ only where PTWRITE is used.  Main images name /ptld/fast/ld.so as interpreter.
# Idempotent.  Output: $RUN_OUT/fastwp_ptw/{lib, poly, rust, mc, py} (same layout as $RUN_OUT/fastwp).
#   bash run/lib/build_ptw.sh [TARGET...]    TARGET in: libs poly rust mc py (default: all); needs build_wp.sh first.
# Env: POLY_CELLS (default: all 30 kernels), PTW_JOBS (parallel rewrites, default nproc/2).
set -u
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}"
RW="$ROOT/ptracer/runtime/jit_toolchain/rewrite_cfr.py"
FAST="$RUN_OUT/fastwp"; PTW="$RUN_OUT/fastwp_ptw"; SPECD="$RUN_OUT/build/spec_wp"; LST="$ROOT/data/ptw_sites"
PYD="$ROOT/suites/pyperformance"; CPY="$PYD/cpython-cg"; INTERP=/ptld/fast/ld.so
POLY_ALL="2mm 3mm adi atax bicg cholesky correlation covariance deriche doitgen durbin fdtd-2d floyd-warshall gemm gemver gesummv gramschmidt heat-3d jacobi-1d jacobi-2d lu ludcmp mvt nussinov seidel-2d symm syr2k syrk trisolv trmm"
JOBS="${PTW_JOBS:-$(( $(nproc) / 2 > 0 ? $(nproc) / 2 : 1 ))}"
[ -x "$FAST/ptld/fast/ld.so" ] || { echo "FATAL: $FAST not built (run run/lib/build_wp.sh first)"; exit 2; }
[ -f "$ROOT/ptracer/runtime/jit_toolchain/e9plugin/ptlog.so" ] || { echo "FATAL: jit_toolchain plugin not built (bash ptracer/build.sh)"; exit 2; }
stale_build "$PTW"
mkdir -p "$PTW/.ids" "$PTW/.log"

# The tree starts as a symlink farm of the Fast tree (merged on every run, so images the Fast build adds later
# appear too); a mixed image replaces its symlink and is never overwritten (-n).
for top in lib poly rust mc py; do
    [ -d "$FAST/$top" ] || continue
    mkdir -p "$PTW/$top" && cp -rs --update=none "$FAST/$top/." "$PTW/$top/"
done

jobs_list() {  # one line per image to build: <list> <original image> <spec> <output> <main 0|1> [flags]
    local t k l real ld sha size rel
    for t in ${*:-libs poly rust mc py}; do case $t in
      libs) for l in libc.so.6 libm.so.6 libgcc_s.so.1 libevent-2.1.so.7; do
                real=$(readlink -f "/lib/x86_64-linux-gnu/$l")
                echo "$LST/nj/lib/$l.ptw|$real|$(spec_of "$real" kf)|$PTW/lib/$l|0|--shared"; done ;;
      poly) for k in ${POLY_CELLS:-$POLY_ALL}; do real="$ROOT/suites/polybench/targets/${k}_large"
                echo "$LST/nj/poly/${k}_large.ptw|$real|$(spec_of "$real" kf)|$PTW/poly/${k}_large|1|"; done ;;
      rust) real="$ROOT/suites/rust/micro-bench/target/release/micro-bench"
            echo "$LST/nj/rust/micro-bench.ptw|$real|$(spec_of "$real" kf)|$PTW/rust/micro-bench|1|" ;;
      mc)   real="$ROOT/suites/memcached/bin/memcached_sym"
            echo "$LST/nj/mc/memcached_sym.ptw|$real|$(spec_of "$real" kf)|$PTW/mc/memcached_sym|1|" ;;
      py)   while IFS=$'\t' read -r real ld sha size; do
                case "$real" in
                  "$CPY"/*)            rel="pyfast/${real#$CPY/}" ;;
                  "$PYD"/wp/site/*)    rel="pysite/${real#$PYD/wp/site/}" ;;
                  "$PYD"/wp/extmods/*) rel="pyext/${real#$PYD/wp/extmods/}" ;;
                  *)                   rel="lib/$(basename "$ld")" ;;
                esac
                case "$real" in */bin/python3.12) m="1|" ;; *) m="0|--shared" ;; esac
                echo "$LST/py/$rel.ptw|$real|$(spec_of "$real" kf)|$PTW/py/$rel|$m"
            done < <(grep -v '^#' "$PYD/wp/images.tsv" | sed "s#@PY@#$PYD#g") ;;
      *) echo "usage: $0 [libs poly rust mc py]" >&2; exit 2 ;;
    esac; done
}
spec_of() { echo "$SPECD/$2/$(echo "$1" | md5sum | cut -c1-6)_$(basename "$1").spec.json"; }

one() {  # <list>|<image>|<spec>|<output>|<main>|<flags>
    IFS='|' read -r lst img spec out main flags <<< "$1"
    [ -f "$lst" ] || return 0                                  # no PTWRITE site: stays the Fast image
    [ -e "$out" ] && [ ! -L "$out" ] && return 0               # built
    local n; n=$(basename "$out"); local log="$PTW/.log/$n.$(echo "$out" | md5sum | cut -c1-6).log"
    [ -s "$spec" ] || { echo "[build_ptw] $n: no Fast spec ($spec): kept the Fast image"; return 0; }
    local ids="$PTW/.ids/$n.$(echo "$out" | md5sum | cut -c1-6).ids"
    "$PYBIN" - "$spec" "$lst" "$ids" <<'PY' || { echo "[build_ptw] $n: site list conversion failed"; return 0; }
import hashlib, json, sys
spec, lst, out = sys.argv[1:4]
sites = json.load(open(spec))["sites"]
rows = [l.strip() for l in open(lst) if l.strip() and not l.startswith("#")]
if rows == ["ALL"]:
    ids = [s["id"] for s in sites]
else:
    want = set(rows)
    def key(s):
        t = {k: v for k, v in s.items() if k != "id"}
        return "%#x %s %s" % (s["addr"], s["when"], hashlib.sha1(json.dumps(t, sort_keys=True).encode()).hexdigest()[:12])
    ids = [s["id"] for s in sites if key(s) in want]
    if len(ids) < len(want):
        print("[build_ptw] %s: %d of %d listed sites not in the local spec (logged through the buffer)"
              % (lst.split("/")[-1], len(want) - len(ids), len(want)), file=sys.stderr)
open(out, "w").write("".join("%d\n" % i for i in ids))
PY
    local tmp="$out.ptwtmp"
    rm -f "$tmp" "$tmp".*
    local ok=0 r sp
    # a PTWRITE site E9Patch cannot place (its trampoline differs) goes back to the buffer sink; an image whose
    # trampolines cannot all be placed with PTW_SPACE fillers is built with the minimum spacing (3) instead
    for sp in $(echo "$PTW_SPACE 3" | tr ' ' '\n' | awk '!s[$0]++'); do
    for r in $(seq 1 10); do
        if "$PYBIN" "$RW" "$spec" "$img" -o "$tmp" --sink mixed --ptw-sites "$ids" --space "$sp" --cfr $flags > "$log" 2>&1; then
            ok=1; break; fi
        grep -qE '^0x[0-9a-fA-F]+' "$tmp.unpatched" 2>/dev/null || break
        "$PYBIN" - "$spec" "$ids" "$tmp.unpatched" <<'PY' || break
import json, sys
spec, ids, unp = sys.argv[1:4]
bad = {int(l.split()[0], 16) for l in open(unp) if l.startswith("0x")}
addr = {s["id"]: s["addr"] for s in json.load(open(spec))["sites"]}
keep = [l for l in open(ids) if addr.get(int(l)) not in bad]
if len(keep) == sum(1 for _ in open(ids)):
    sys.exit(1)                     # not a PTWRITE site: nothing to move
open(ids, "w").writelines(keep)
print("[build_ptw] %s: %d unpatched site(s) moved to the buffer sink" % (spec.split("/")[-1], len(bad)), file=sys.stderr)
PY
        rm -f "$tmp" "$tmp".*
    done
    [ "$ok" = 1 ] && break
    done
    if [ "$ok" = 1 ] && { [ "$main" = 0 ] || "$PYBIN" run/lib/set_interp.py "$tmp" "$INTERP" >> "$log" 2>&1; }; then
        rm -f "$out"; mv -f "$tmp" "$out"
        for f in "$tmp".*; do [ -e "$f" ] && mv -f "$f" "$out${f#$tmp}"; done
        echo "$sp" > "$out.ptw_space"
        echo "[build_ptw] $n: mixed OK ($(grep -o '[0-9.]* % PTWRITE' "$log" | head -1), spacing $sp)"
    else
        rm -f "$tmp"
        echo "[build_ptw] $n: mixed build FAILED (log $log): kept the Fast image"
    fi
}
export -f one; export PTW PYBIN RW INTERP ROOT E9PATCH_DIR PTW_SPACE
t0=$(date +%s)
jobs_list "$@" | xargs -d '\n' -P "$JOBS" -I{} bash -c 'one "$1"' _ {}
echo "[build_ptw] Fast-PTWRITE tree ready under $PTW ($(( $(date +%s) - t0 )) s)"
