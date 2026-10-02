#!/usr/bin/env bash
# Host-side environment check for the PTracer artifact.  Run it on the bare-metal HOST
# (not inside Docker) before docker/build.sh.  Plain bash + coreutils; gcc is used when
# present for two small probes (per-core CPUID, DRAM bandwidth) and skipped otherwise.
#
#   bash envcheck/check_env.sh            # exit 0 = every hard check passed (warnings allowed)
#
# Hard checks (FAIL): Intel CPU; Intel PT PMU present; >= 8 online cores; >= 28 GiB RAM; >= 40 GB free disk.
# PT required; PTWRITE only for the *-PTWRITE bars (WARN without it: every other configuration executes no PTWRITE).
# Soft checks (WARN): PTWRITE on every online core, perf_event_paranoid (the privileged container bypasses it), free disk
# < 100 GB, disk-write and DRAM bandwidth below thresholds, turbo / ASLR not controllable,
# hybrid P/E cores, SMT on, kernel newer than tested.  Nothing on the host is changed.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAIL=0; WARN=0
pass() { printf '  PASS  %s\n' "$*"; }
warn() { printf '  WARN  %s\n' "$*"; WARN=$((WARN+1)); }
fail() { printf '  FAIL  %s\n' "$*"; FAIL=$((FAIL+1)); }
info() { printf '        %s\n' "$*"; }

TMP="$(mktemp -d "$ROOT/.envcheck.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
HAVE_GCC=0; command -v gcc >/dev/null 2>&1 && HAVE_GCC=1
IS_ROOT=0; [ "$(id -u)" = 0 ] && IS_ROOT=1

echo "== PTracer artifact: host environment check =="
echo "   kernel $(uname -r)   $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
echo

# ---------------------------------------------------------------- hard checks
echo "-- hard requirements --"

if grep -q -m1 'GenuineIntel' /proc/cpuinfo; then pass "Intel CPU"
else fail "not an Intel CPU (Intel PT and PTWRITE are Intel-only)"; fi

PT=/sys/bus/event_source/devices/intel_pt
if [ -d "$PT" ]; then
  pass "Intel PT PMU present ($PT)"
else
  fail "no Intel PT PMU at $PT (kernel without intel_pt, a VM without PT pass-through, or a CPU without PT)"
fi

PTS=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)
if [ "$PTS" = 0 ]; then
  pass "kernel.yama.ptrace_scope=0"
else
  warn "kernel.yama.ptrace_scope=$PTS: the Java HiFi bars need 0 (Pin 4.4 attaches to a child the JVM execs): sudo sysctl -w kernel.yama.ptrace_scope=0"
fi
PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 99)
PT_OK_HERE=1
if [ "$IS_ROOT" = 1 ] || [ "$PARANOID" -le 1 ]; then
  pass "perf_event_paranoid=$PARANOID (root=$IS_ROOT)"
else
  PT_OK_HERE=0
  warn "perf_event_paranoid=$PARANOID: this host user cannot open Intel PT, but the --privileged container (docker/run.sh) can; nothing to change unless PT fails inside the container too (then: sudo sysctl kernel.perf_event_paranoid=1)"
fi

# Open an Intel PT event on this process: PMU + permission + no seccomp block.  Only decisive
# when this user is allowed to (root or paranoid <= 1).
if [ -d "$PT" ] && [ "$HAVE_GCC" = 1 ]; then
  cat > "$TMP/ptopen.c" <<'C'
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
int main(int argc,char**argv){ struct perf_event_attr a; memset(&a,0,sizeof a);
  a.type=atoi(argv[1]); a.size=sizeof a; a.exclude_kernel=1; a.exclude_hv=1; a.disabled=1;
  int fd=syscall(SYS_perf_event_open,&a,0,-1,-1,0);
  if(fd<0){ printf("%s\n",strerror(errno)); return 1; } close(fd); return 0; }
C
  if gcc -O0 -o "$TMP/ptopen" "$TMP/ptopen.c" 2>/dev/null; then
    ERR=$("$TMP/ptopen" "$(cat $PT/type)" 2>&1)
    if [ $? -eq 0 ]; then pass "perf_event_open(intel_pt) works"
    elif [ "$PT_OK_HERE" = 0 ]; then warn "perf_event_open(intel_pt) refused for this user ($ERR); expected with perf_event_paranoid=$PARANOID, the privileged container is the real test"
    else fail "perf_event_open(intel_pt) failed: $ERR (kernel PT support / a VM without PT / seccomp?)"; fi
  else warn "could not compile the perf_event_open probe; skipped"; fi
elif [ -d "$PT" ]; then
  warn "gcc not found: perf_event_open(intel_pt) not probed"
fi

# PTWRITE (PT required; PTWRITE only for the *-PTWRITE bars): the PMU advertises it (what the kernel read at boot) and CPUID.(14H,0):EBX[4]
# on every online core (hybrid parts can differ per core).
if [ "$(cat $PT/caps/ptwrite 2>/dev/null)" = 1 ]; then
  pass "PTWRITE advertised by the PT PMU"
else
  warn "PTWRITE not advertised ($PT/caps/ptwrite != 1): PT required; PTWRITE only for the *-PTWRITE bars (HiFi-PTWRITE, Fast-PTWRITE; 4th-gen Xeon Scalable / 12th-gen Core P-cores or newer, Goldmont Plus / Tremont / Gracemont) -- skip those bars on this machine"
fi
ONLINE=$(cat /sys/devices/system/cpu/online 2>/dev/null)
CORES=$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc)
if [ "$HAVE_GCC" = 1 ]; then
  cat > "$TMP/ptw.c" <<'C'
#include <cpuid.h>
#include <stdio.h>
int main(void){ unsigned a,b,c,d; if(__get_cpuid_max(0,0)<0x14) return 2;
  __cpuid_count(0x14,0,a,b,c,d); return (b&(1u<<4))?0:1; }
C
  if gcc -O0 -o "$TMP/ptw" "$TMP/ptw.c" 2>/dev/null && command -v taskset >/dev/null 2>&1; then
    MISSING=""
    for c in $(echo "$ONLINE" | tr ',' ' ' | while read -r r; do
                 case $r in *-*) seq "${r%-*}" "${r#*-}";; *) echo "$r";; esac; done); do
      taskset -c "$c" "$TMP/ptw" || MISSING="$MISSING $c"
    done
    if [ -z "$MISSING" ]; then pass "PTWRITE (CPUID 14H) on all online cores ($ONLINE)"
    else warn "PTWRITE missing on core(s):$MISSING (PT required; PTWRITE only for the *-PTWRITE bars)"; fi
  else warn "per-core PTWRITE CPUID probe skipped (gcc/taskset unavailable)"; fi
fi

if [ "$CORES" -ge 8 ]; then pass "$CORES online cores (>= 8)"
else fail "$CORES online cores; need >= 8 (traced program, PT drain and helper threads are pinned to separate cores)"; fi

MEMKB=$(awk '/MemTotal/{print $2}' /proc/meminfo)
MEMGIB=$((MEMKB/1024/1024))
# 28 GiB is what a 32 GB machine reports after firmware reservations.  Offline reconstruction
# of one accuracy cell holds the decoded PT trace plus the value log in memory (>16 GB seen);
# the traced program and the page cache for the trace files need the rest.
if [ "$MEMGIB" -ge 28 ]; then pass "${MEMGIB} GiB RAM (>= 28 GiB, i.e. a 32 GB machine)"
else fail "${MEMGIB} GiB RAM; need a 32 GB machine (reconstruction of one accuracy cell exceeds 16 GB)"; fi

FREEKB=$(df -Pk "$ROOT" | awk 'NR==2{print $4}')
FREEGB=$((FREEKB/1000/1000))
# 40 GB: vendored kits + Docker image + builds.  100 GB: the oracle re-runs write multi-GB
# PT + value-log files per accuracy cell.
if [ "$FREEGB" -ge 100 ]; then pass "${FREEGB} GB free on $(df -P "$ROOT" | awk 'NR==2{print $6}')"
elif [ "$FREEGB" -ge 40 ]; then warn "${FREEGB} GB free: enough for the build and the overhead experiment; keep >= 100 GB free before the inaccuracy experiment, which writes multi-GB traces"
else fail "${FREEGB} GB free; need >= 40 GB (100 GB for the full re-measure)"; fi

# Disk write bandwidth on the artifact's own directory (this is where traces would land).
if command -v dd >/dev/null 2>&1; then
  OUT=$(dd if=/dev/zero of="$TMP/ddtest" bs=16M count=64 oflag=direct conv=fsync 2>&1 | tail -1)
  echo "$OUT" | grep -q 'copied' \
    || OUT=$(dd if=/dev/zero of="$TMP/ddtest" bs=16M count=64 conv=fsync 2>&1 | tail -1)
  rm -f "$TMP/ddtest"
  MBS=$(echo "$OUT" | awk '{for(i=1;i<=NF;i++) if($i ~ /B\/s/){v=$(i-1); u=$i}} END{ if(u ~ /^GB/) v=v*1000; if(u ~ /^kB/) v=v/1000; printf "%d", v }')
  # A live capture writes the PT stream + value log at 400-900 MB/s per traced thread; below
  # that the accuracy re-runs may overflow the PT ring (lost packets show up as unknown addresses).
  # Timing runs discard both streams, so overhead numbers are not affected.
  if [ "${MBS:-0}" -ge 1000 ]; then pass "sequential disk write ${MBS} MB/s (1 GiB, >= 1000 MB/s)"
  else warn "sequential disk write ${MBS:-?} MB/s (< 1000 MB/s): accuracy re-runs may lose PT packets or take much longer"; fi
fi

# DRAM bandwidth (single thread, 256 MiB buffers: write = memset, read = strided sum, copy = memcpy).
if [ "$HAVE_GCC" = 1 ]; then
  cat > "$TMP/membw.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){ size_t n=(size_t)256<<20; char *a=malloc(n),*b=malloc(n); if(!a||!b) return 1;
  memset(a,1,n); memset(b,2,n); double w=0,r=0,c=0; volatile long sink=0;
  for(int i=0;i<3;i++){double t=now(); memset(a,i,n); t=now()-t; if(n/t/1e9>w) w=n/t/1e9;}
  for(int i=0;i<3;i++){double t=now(); long s=0,*p=(long*)a; for(size_t k=0;k<n/8;k+=8) s+=p[k]; sink+=s; t=now()-t; if(n/t/1e9>r) r=n/t/1e9;}
  for(int i=0;i<3;i++){double t=now(); memcpy(b,a,n); t=now()-t; if(2.0*n/t/1e9>c) c=2.0*n/t/1e9;}
  printf("%.1f %.1f %.1f\n",w,r,c); return 0; }
C
  if gcc -O2 -o "$TMP/membw" "$TMP/membw.c" 2>/dev/null; then
    read -r BW_W BW_R BW_C < <("$TMP/membw")
    # The buffer sink streams 16-byte records to DRAM; any DDR4/DDR5 desktop or server sustains
    # > 8 GB/s single-thread write.  Below 5 GB/s the machine (or a VM) is not representative.
    if awk "BEGIN{exit !($BW_W >= 5)}"; then pass "DRAM bandwidth write ${BW_W} GB/s, read ${BW_R} GB/s, copy ${BW_C} GB/s (single thread)"
    else warn "DRAM write bandwidth ${BW_W} GB/s (< 5 GB/s): buffer-sink overhead will be higher than the paper's"; fi
  else warn "DRAM bandwidth probe did not compile; skipped"; fi
else
  warn "gcc not found: DRAM bandwidth not measured"
fi

# ---------------------------------------------------------------- soft checks
echo
echo "-- soft checks (warnings only) --"

NT=/sys/devices/system/cpu/intel_pstate/no_turbo
BOOST=/sys/devices/system/cpu/cpufreq/boost
if [ -f "$NT" ]; then
  if [ "$IS_ROOT" = 1 ] || [ -w "$NT" ]; then pass "turbo controllable via $NT (now no_turbo=$(cat $NT))"
  else warn "turbo control needs root ($NT, now no_turbo=$(cat $NT)); run docker/run.sh with sudo, or performance may not match the paper"; fi
elif [ -f "$BOOST" ]; then
  pass "turbo controllable via $BOOST (now boost=$(cat $BOOST))"
else
  warn "turbo cannot be disabled (no intel_pstate/no_turbo or cpufreq/boost): performance may not match the paper"
fi

if setarch "$(uname -m)" -R true 2>/dev/null; then pass "ASLR can be disabled per process (setarch -R); randomize_va_space=$(cat /proc/sys/kernel/randomize_va_space 2>/dev/null)"
else warn "ASLR cannot be disabled (setarch -R failed): accuracy results may not reproduce exactly"; fi

if [ -d /sys/devices/cpu_core ] && [ -d /sys/devices/cpu_atom ]; then
  warn "hybrid CPU: P-cores $(cat /sys/devices/cpu_core/cpus), E-cores $(cat /sys/devices/cpu_atom/cpus). PTWRITE arms overflow on E-cores; the run scripts pin to P-cores -- do not move them"
else
  pass "homogeneous cores"
fi

SMT=$(cat /sys/devices/system/cpu/smt/active 2>/dev/null || echo unknown)
if [ "$SMT" = 1 ]; then warn "SMT is enabled: higher run-to-run variance (optional: echo off | sudo tee /sys/devices/system/cpu/smt/control)"
else pass "SMT active=$SMT"; fi

KMAJ=$(uname -r | cut -d. -f1); KMIN=$(uname -r | cut -d. -f2)
# Tested kernels: 6.8 - 7.0.  Pin's own kernel-version check is advisory and every driver
# passes -ifeellucky; Pin 3.20 is only used by the optional baselines.  A kernel newer than
# tested may break Pin (HiFi rows); PT capture and the Fast rows do not depend on Pin.
if [ "$KMAJ" -gt 7 ] || { [ "$KMAJ" -eq 7 ] && [ "$KMIN" -gt 0 ]; }; then
  warn "kernel $(uname -r) is newer than the tested 6.8-7.0: Pin 4.4 (HiFi rows) may fail to attach; PT and the Fast rows are unaffected"
elif [ "$KMAJ" -lt 5 ]; then
  warn "kernel $(uname -r) is older than tested (6.8-7.0); Intel PT support before 5.x is incomplete"
else pass "kernel $(uname -r) (tested: 6.8-7.0)"; fi

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then pass "docker usable"
else warn "docker not usable from this user (docker/build.sh and docker/run.sh need it; try sudo)"; fi

# ---------------------------------------------------------------- summary
echo
echo "== EXPECTATION SUMMARY =="
cat <<'T'
  Reproduces on any qualifying machine: every figure's SHAPE and ordering --
    which suites are cheap and which are expensive, HiFi vs Fast, the ablation regimes, and the
    accuracy cells (PolyBench 0 wrong/unknown, pyperformance < 0.1 %).
  May differ when re-measured: absolute slowdown magnitudes.  They depend on the
    microarchitecture (PTWRITE cost, trampoline cost, Pin's code-cache cost), on the
    frequency lock, and on the machine being quiet.  Expect the same order of magnitude.
  What the warnings above imply:
    turbo not disabled  -> ratios drift by a few percent run to run; magnitudes shift.
    ASLR not disabled   -> accuracy cells can differ in which addresses are unknown; the
                           < 0.1 % bar should still hold.
    hybrid P/E cores    -> keep the pinning; a PTWRITE row measured on an E-core is invalid.
    SMT on              -> more variance; use the reported medians.
    slow disk / DRAM    -> accuracy re-runs (which write traces) get slower or lose packets;
                           overhead runs discard the streams and are unaffected.
  Without root: run/fig5.sh --dry-run needs nothing, not even Docker if
    python3 + matplotlib/numpy are installed.  A user in the docker group can run every
    re-measurement (the privileged container opens Intel PT itself).  sudo is used by docker/run.sh
    for kernel.yama.ptrace_scope=0 during run/fig5.sh (Java HiFi bars) and for the optional turbo
    lock (QUIET=1); without the lock magnitudes drift a few percent.
T
echo
if [ "$FAIL" -eq 0 ]; then echo "RESULT: OK  (warnings: $WARN)"; exit 0; fi
echo "RESULT: FAIL ($FAIL hard check(s) failed, $WARN warning(s))"; exit 1
