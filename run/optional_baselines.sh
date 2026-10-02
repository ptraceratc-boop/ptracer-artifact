#!/usr/bin/env bash
# Kept for compatibility: the traditional-tracer baselines are now run/fig5_traditional.sh (same arguments:
# memtrace | libdft | valgrind | spindle ...; BASELINES_CSV still overrides the output CSV).
exec bash "$(dirname "$0")/fig5_traditional.sh" "$@"
