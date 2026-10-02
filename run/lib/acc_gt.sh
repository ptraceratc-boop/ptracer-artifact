#!/usr/bin/env bash
# Ground-truth twins of the Figure 5 Fast / Fast-PTWRITE images for run/02_accuracy.sh:
#   $ACC_DIR/gt/tnt/...  Fast: runtime/rewrite.py --sink buffer --cfr --sync-carrier tnt (as build_wp.sh)
#   $ACC_DIR/gt/ptw/...  Fast-PTWRITE: images that have a mixed Figure 5 build -> jit_toolchain/rewrite_cfr.py
#                        --sink mixed --ptw-sites <that build's site ids> --space $PTW_SPACE --cfr (as build_ptw.sh);
#                        every other image is the tnt twin, as in Figure 5
# Same spec and flags as the Figure 5 image, plus `--gt-all --allow-gt-drop --gt-retry 12'.  An image whose twin cannot
# be built (E9Patch cannot place every plan site next to the probes) keeps its Figure 5 image; its accesses are then
# not compared (reported per cell as ground-truth coverage).
# No twin: ld.so, libcrypto, libffi (built with --keep-going in Figure 5) and libevent (Memcached only).
#   bash run/lib/acc_gt.sh [poly] [py]       Idempotent.  Env: ACC_DIR, ACC_POLY, JOBS.
set -u
. "$(dirname "$0")/common.sh"
cd "$ROOT"
pick_python
pick_out
export E9PATCH_DIR="${E9PATCH_DIR:-$ROOT/third_party/e9patch}" PYBIN
FAST=$RUN_OUT/fastwp; PTW=$RUN_OUT/fastwp_ptw; SPECD=$RUN_OUT/build/spec_wp
G="${ACC_DIR:?}/gt"
stale_build "$G"
mkdir -p "$G/log"
RWF=$ROOT/ptracer/runtime/rewrite.py; RWM=$ROOT/ptracer/runtime/jit_toolchain/rewrite_cfr.py
GTF="--gt-all --allow-gt-drop --gt-retry 12"
PYD=$ROOT/suites/pyperformance; CPY=$PYD/cpython-cg
spec_of() { echo "$SPECD/kf/$(echo "$1" | md5sum | cut -c1-6)_$(basename "$1").spec.json"; }
for m in tnt ptw; do mkdir -p "$G/$m" && cp -rs --update=none "$FAST/." "$G/$m/"; done   # symlink farm; twins replace their link

items() {  # orig|spec|rel|main|flags
    local t k o l
    for t in "$@"; do case $t in
      poly) for k in ${ACC_POLY:?}; do o=$ROOT/suites/polybench/targets/${k}_large
                echo "$o|$(spec_of "$o")|poly/${k}_large|1|"; done
            for l in libc.so.6 libm.so.6 libgcc_s.so.1; do o=$(readlink -f "/lib/x86_64-linux-gnu/$l")
                echo "$o|$(spec_of "$o")|lib/$l|0|--shared"; done ;;
      py)   grep -v '^#' "$PYD/wp/images.tsv" | sed "s#@PY@#$PYD#g" | while IFS=$'\t' read -r real ld sha size; do
                case "$real" in */libcrypto.so.3|*/libffi.so.8.1.4) continue ;; esac
                case "$real" in
                  "$CPY"/*)            rel="py/pyfast/${real#$CPY/}" ;;
                  "$PYD"/wp/site/*)    rel="py/pysite/${real#$PYD/wp/site/}" ;;
                  "$PYD"/wp/extmods/*) rel="py/pyext/${real#$PYD/wp/extmods/}" ;;
                  *)                   rel="py/lib/$(basename "$ld")" ;;
                esac
                case "$real" in */bin/python3.12) echo "$real|$(spec_of "$real")|$rel|1|" ;;
                                *) echo "$real|$(spec_of "$real")|$rel|0|--shared" ;; esac
            done ;;
      *) echo "usage: $0 [poly] [py]" >&2; exit 2 ;;
    esac; done
}
fiximg() {  # <image>: the site map names the image by the path it was written to -> its final path
    "$PYBIN" -c 'import json,sys; p=sys.argv[1]+".sitemap.json"; m=json.load(open(p)); m["image"]=sys.argv[1]; json.dump(m,open(p,"w"))' "$1"; }
twin() {  # <log> <tmp> <main> <rewriter command...>
    local log="$1" t="$2" main="$3"; shift 3
    "$@" > "$log" 2>&1 && { [ "$main" = 0 ] || "$PYBIN" run/lib/set_interp.py "$t" /ptld/fast/ld.so >> "$log" 2>&1; }
}
place() {  # <tmp> <final>: move a built twin (and its side files) into place
    local t="$1" o="$2" f
    rm -f "$o"; mv -f "$t" "$o"
    for f in "$t".*; do [ -e "$f" ] && { rm -f "$o${f#$t}"; mv -f "$f" "$o${f#$t}"; }; done
    fiximg "$o"
}
one() {
    IFS='|' read -r orig spec rel main flags <<< "$1"
    local n; n=$(echo "$rel" | tr / _)
    [ -s "$spec" ] || { echo "[acc_gt] $rel: no Figure 5 spec, not compared"; return 0; }
    local o="$G/tnt/$rel" t
    if [ -L "$o" ] || [ ! -e "$o" ]; then
        t="$o.gttmp"; rm -f "$t" "$t".*
        if twin "$G/log/$n.tnt.log" "$t" "$main" "$PYBIN" "$RWF" "$spec" "$orig" -o "$t" --sink buffer --cfr --sync-carrier tnt $flags $GTF; then
            place "$t" "$o"; echo "[acc_gt] Fast $rel: OK"
        else rm -f "$t" "$t".*; echo "[acc_gt] Fast $rel: twin FAILED, not compared (log $G/log/$n.tnt.log)"; fi
    fi
    local p="$G/ptw/$rel" ids="$PTW/.ids/$(basename "$rel").$(echo "$PTW/$rel" | md5sum | cut -c1-6).ids"
    if [ -f "$PTW/$rel" ] && [ ! -L "$PTW/$rel" ] && [ -f "$ids" ]; then
        if [ -L "$p" ] || [ ! -e "$p" ]; then
            t="$p.gttmp"; rm -f "$t" "$t".*
            if twin "$G/log/$n.ptw.log" "$t" "$main" "$PYBIN" "$RWM" "$spec" "$orig" -o "$t" --sink mixed --ptw-sites "$ids" \
                   --space "$(cat "$PTW/$rel.ptw_space" 2>/dev/null || echo "$PTW_SPACE")" --cfr $flags $GTF; then
                place "$t" "$p"; echo "[acc_gt] Fast-PTWRITE $rel: OK"
            else
                rm -f "$t" "$t".*; echo "[acc_gt] Fast-PTWRITE $rel: twin FAILED, not compared (log $G/log/$n.ptw.log)"
                ln -sfn "$PTW/$rel" "$p"; for f in "$PTW/$rel".*; do [ -e "$f" ] && ln -sfn "$f" "$p${f#$PTW/$rel}"; done
            fi
        fi
    else   # Fast-PTWRITE uses the Fast image here: the tnt twin
        [ -e "$o" ] && [ ! -L "$o" ] && ln -sfn "$o" "$p"
        for f in "$o".*; do [ -e "$f" ] && ln -sfn "$f" "$p${f#$o}"; done
    fi
    return 0
}
export -f one place fiximg twin; export G RWF RWM GTF PTW PTW_SPACE ROOT
items ${*:-poly py} | xargs -d '\n' -P "${JOBS:-$(nproc)}" -I{} bash -c 'one "$1"' _ {}
# CPython's compiled stdlib modules (as after any earlier run of the interpreter): a traced run then imports them
# instead of compiling the sources, as in the overhead runs
for m in tnt ptw; do
    [ -d "$G/$m/py/pyfast/lib/python3.12" ] || continue
    "$CPY/bin/python3.12" -m compileall -q -j "${JOBS:-$(nproc)}" "$G/$m/py/pyfast/lib/python3.12" "$G/$m/py/pyext" "$G/$m/py/pysite" \
        > "$G/log/compileall.$m.log" 2>&1 || echo "[acc_gt] compileall $m: some modules did not compile (see $G/log/compileall.$m.log)"
done
echo "[acc_gt] ground-truth twins ready under $G"
