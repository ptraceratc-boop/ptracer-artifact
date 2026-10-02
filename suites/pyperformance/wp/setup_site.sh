#!/usr/bin/env bash
# Whole-process pyperformance: unpack the benchmarks' third-party packages (site.tar.xz; requirements.txt = their
# `pip freeze`) into suites/pyperformance/wp/site, then check that all 82
# images a pyperformance process maps (images.tsv: interpreter, lib-dynload, extmods, site .so, system libs) have
# the exact bytes the Fast images and HiFi plans were built from.  Idempotent; run by the Dockerfile.
#   PYBIN=<python 3.12 with pip> bash suites/pyperformance/wp/setup_site.sh [--check-only]
set -eu
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; PY="$(dirname "$D")"
PYBIN="${PYBIN:-python3}"
if [ "${1:-}" != --check-only ] && [ ! -f "$D/site/.complete" ]; then
    # site.tar.xz = the installed packages every analysis is keyed to (requirements.txt = their `pip freeze`).
    # Shipped as built bytes: several pinned versions have no Python 3.12 wheel, and a source build embeds its
    # temporary build paths, so a reinstall would not reproduce the images the plans address.
    "$PYBIN" -c 'import sys, tarfile; tarfile.open(sys.argv[1]).extractall(sys.argv[2])' "$D/site.tar.xz" "$D"
    touch "$D/site/.complete"
fi
bad=0; n=0
while IFS=$'\t' read -r real ld sha size; do
    case "$real" in \#*) continue;; esac
    f="${real//@PY@/$PY}"; n=$((n+1))
    if [ ! -f "$f" ]; then echo "MISSING $f"; bad=$((bad+1)); continue; fi
    x=$(sha256sum "$f" | cut -c1-16)
    [ "$x" = "$sha" ] || { echo "MISMATCH $f sha256 $x (expected $sha)"; bad=$((bad+1)); }
done < "$D/images.tsv"
echo "[setup_site] $n images checked, $bad differ"
[ $bad = 0 ] || { echo "[setup_site] FATAL: the pyperformance images differ from the ones the plans were built on"; exit 3; }
