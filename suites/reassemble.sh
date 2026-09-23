#!/bin/bash
# Reassemble the files that were split into <name>.part-NN pieces (GitHub's 100 MB limit),
# restore their mode, and verify each against SPLIT.sha256.  Idempotent: a file that is
# already present and verifies is left alone.  Run from anywhere.
set -eu
cd "$(dirname "$0")"
rc=0
while read -r sum path; do
  [ -n "$path" ] || continue
  if [ -f "$path" ] && echo "$sum  $path" | sha256sum -c --quiet >/dev/null 2>&1; then
    echo "ok       $path"; continue
  fi
  parts=$(ls "$path".part-* 2>/dev/null | sort) || true
  if [ -z "$parts" ]; then
    if [ ! -d "$(dirname "$path")" ]; then echo "SKIP     $path (suite not shipped in this version)"; continue; fi
    echo "MISSING  $path (no parts found)"; rc=1; continue
  fi
  cat $parts > "$path"
  if echo "$sum  $path" | sha256sum -c --quiet; then
    echo "rebuilt  $path"
    case "$path" in *bin/*) chmod 755 "$path";; *) chmod 644 "$path";; esac
  else
    echo "BAD      $path (checksum mismatch)"; rm -f "$path"; rc=1
  fi
done < SPLIT.sha256
exit $rc
