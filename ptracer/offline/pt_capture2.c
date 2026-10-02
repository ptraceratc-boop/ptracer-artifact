/*
 * pt_capture2.c -- capture an Intel PT trace of one command for the offline reconstructor.
 *   --aux-out FILE    write the raw AUX (PT packet) bytes to FILE
 *   --sideband FILE   write a JSON sideband: pid, exit status, the union of the
 *                     traced process's /proc/PID/maps (polled while it runs), CPU
 *                     family/model/stepping, CPUID.15H, MTC period, perf clock params.
 * It can also report packet-level statistics, with a focus on OVF (buffer overflow) packets.
 *
 * Approach (raw perf_event_open + libipt, no `perf` CLI needed):
 *   1. Configure an intel_pt event with BRANCH tracing + TSC + MTC timing
 *      (user space only), disabled, enable-on-exec.
 *   2. fork(); the parent opens the event for the child PID, maps a large AUX
 *      buffer, then releases the child (via a pipe) to exec the benchmark.  The
 *      trace auto-enables on exec and flows into AUX.
 *   3. After the child exits, scan the AUX bytes with libipt's PACKET decoder
 *      (image-free) and count packet types, especially ppt_ovf.
 *
 * Usage: pt_capture2 [--aux-mb N] [--mtc-period P] [--ptw] [--no-ptw] [--aux-out F] [--sideband F] [--no-decode] [--no-drain] [--child-env V=X] -- <cmd> [args...]
 *   --scan   packet-count summary (default)
 *   exit 0 = captured & scanned; 2 = setup error.
 *
 * MULTI-THREADED CAPTURE.  `--cpu N' is REPEATABLE:
 * one per-CPU intel_pt event per core, the child pinned to exactly that set of cores, one AUX
 * file per core (`<aux-out>.cpu<N>' when there is more than one) and ONE sideband.  Every
 * per-CPU event records `context_switch' records (PERF_RECORD_SWITCH_CPU_WIDE /
 * ITRACE_START with PERF_SAMPLE_TID|TIME|CPU) which the drain thread copies into a binary
 * switch file per core (`<sideband>.cpu<N>.sw', struct sw_rec below), so the offline stage
 * can attribute every [TIP.PGE, TIP.PGD] region of a core's AUX stream to the thread that
 * ran it.  With `--sideband' the child is PTRACE_SEIZEd with PTRACE_O_TRACECLONE as well, so
 * every thread it creates is seen at its first stop and its `fs_base' (set by the kernel from
 * CLONE_SETTLS before the thread runs) is read with PTRACE_GETREGSET and written to the
 * sideband's `threads' list.  A single --cpu / per-task capture writes the same files and
 * JSON keys, plus the multi-threaded keys.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <time.h>
#include <sched.h>
#include <intel-pt.h>
#include <cpuid.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <poll.h>
#include "../runtime/rt/ptlog_abi.h"   /* The runtime's spare-TCB ring */

/* ---- sideband: union of the target's mappings, snapshotted while it runs ----
 *
 * Recorded: every file-backed mapping, plus every ANONYMOUS EXECUTABLE mapping
 * -- above all `[vdso]'.  The vDSO is real, executed code (every
 * clock_gettime()/gettimeofday() call runs in it), so leaving it out of the
 * sideband makes libipt fail to decode there, lose synchronisation, and drop
 * every PTWRITE payload up to the next PSB.  There is no file to point libipt
 * at, so its bytes are dumped out of the target's own memory (/proc/PID/mem
 * while the target is at a ptrace stop) into <sideband>.<name>.bin and the
 * sideband names that file; the mapping keeps its kernel name in "name" so the
 * comparison tools can still recognise it as [vdso].
 *
 * The snapshots are taken only AFTER the child has execve()d (PTRACE_O_TRACEEXEC
 * stop) and again at its exit stop: polling before the exec recorded
 * pt_capture2's OWN image, its libc and libipt, whose addresses overlap the
 * target's mappings and corrupt the decode.
 */
/* The map list is DYNAMIC: a whole-program image under E9Patch can have tens of
 * thousands of distinct mappings (the E9Patch loader maps every trampoline page
 * separately), and a dropped mapping silently breaks the decode.  Nothing is
 * dropped; if the table cannot grow the capture ABORTS rather than truncate. */
#define MAPS_HARD_CAP 4000000
struct mapent { uint64_t start, end, off; char perms[8]; char path[512]; char name[512]; };
static struct mapent *g_maps = NULL; static size_t g_nmaps = 0, g_capmaps = 0;
static const char *g_dumpdir = NULL;       /* where anonymous code is dumped (= the sideband path) */
static const char *g_trace_control_path = NULL;
static uint64_t g_gate_enabled_ns = 0, g_gate_disabled_ns = 0;
static unsigned g_gate_transitions = 0;
static int g_ndumps = 0;
static uint64_t g_nstops = 0, g_nsigill = 0, g_nsigtrap = 0;   /* SIGTRAP: forwarded */
static int g_swallow_trap = 0;             /* PT_CAPTURE2_SWALLOW_SIGTRAP=1: suppress SIGTRAP (diagnostic only) */
static uint64_t g_maps_dropped = 0;        /* must stay 0; reported loudly if not */

/* (start,end,name) -> index, so poll_maps() is O(1) per line instead of O(#maps).
 * At 17 000 mappings the old linear scan made every /proc/PID/maps snapshot cost
 * ~300 M string compares, and the snapshot is taken hundreds of times per run. */
static uint32_t *g_hidx = NULL; static size_t g_hcap = 0;   /* index+1, 0 = empty */
static uint64_t map_hash(uint64_t s, uint64_t e, const char *nm) {
    uint64_t h = 1469598103934665603ull;
    h = (h ^ s) * 1099511628211ull; h = (h ^ e) * 1099511628211ull;
    for (const unsigned char *p = (const unsigned char *)nm; *p; p++) h = (h ^ *p) * 1099511628211ull;
    return h ? h : 1;
}
static void hidx_insert(size_t idx) {
    const struct mapent *m = &g_maps[idx];
    size_t i = (size_t)(map_hash(m->start, m->end, m->name) & (g_hcap - 1));
    while (g_hidx[i]) i = (i + 1) & (g_hcap - 1);
    g_hidx[i] = (uint32_t)(idx + 1);
}
static void hidx_rebuild(size_t cap) {
    free(g_hidx); g_hcap = cap;
    g_hidx = calloc(g_hcap, sizeof *g_hidx);
    if (!g_hidx) { fprintf(stderr, "error: out of memory for the map index\n"); exit(2); }
    for (size_t i = 0; i < g_nmaps; i++) hidx_insert(i);
}
/* Returns the index of the entry with this (start,end,name), or -1. */
static long hidx_find(uint64_t s, uint64_t e, const char *nm) {
    if (!g_hcap) return -1;
    size_t i = (size_t)(map_hash(s, e, nm) & (g_hcap - 1));
    while (g_hidx[i]) {
        struct mapent *m = &g_maps[g_hidx[i] - 1];
        if (m->start == s && m->end == e && !strcmp(m->name, nm)) return (long)(g_hidx[i] - 1);
        i = (i + 1) & (g_hcap - 1);
    }
    return -1;
}
/* Append `m'.  Fails LOUDLY (and fatally) rather than dropping a mapping. */
static void maps_append(const struct mapent *m) {
    if (g_nmaps == g_capmaps) {
        size_t nc = g_capmaps ? g_capmaps * 2 : 1024;
        if (nc > MAPS_HARD_CAP) {
            g_maps_dropped++;
            fprintf(stderr, "error: more than %d mappings; the sideband would be TRUNCATED "
                            "and the decode would lose most of the trace -- aborting\n", MAPS_HARD_CAP);
            exit(2);
        }
        struct mapent *nm = realloc(g_maps, nc * sizeof *nm);
        if (!nm) {
            g_maps_dropped++;
            fprintf(stderr, "error: out of memory growing the map table to %zu entries -- "
                            "the sideband would be TRUNCATED, aborting\n", nc);
            exit(2);
        }
        g_maps = nm; g_capmaps = nc;
    }
    g_maps[g_nmaps] = *m;
    g_nmaps++;
    if (g_nmaps > g_hcap / 2) hidx_rebuild(g_hcap ? g_hcap * 2 : 4096);   /* rebuild indexes it too */
    else hidx_insert(g_nmaps - 1);
}

/* Read [start,end) out of the traced process and write it to `out'. */
static int dump_range(pid_t pid, uint64_t start, uint64_t end, const char *out) {
    char fn[64]; snprintf(fn, sizeof fn, "/proc/%d/mem", (int)pid);
    int fd = open(fn, O_RDONLY); if (fd < 0) return -1;
    size_t len = (size_t)(end - start);
    unsigned char *buf = malloc(len);
    if (!buf) { close(fd); return -1; }
    ssize_t got = pread(fd, buf, len, (off_t)start);
    close(fd);
    if (got != (ssize_t)len) { free(buf); return -1; }
    FILE *g = fopen(out, "wb");
    if (!g) { free(buf); return -1; }
    size_t w = fwrite(buf, 1, len, g);
    fclose(g); free(buf);
    return w == len ? 0 : -1;
}

/* Anonymous ranges whose bytes could not be dumped: remembered so the polling
 * loop does not retry (and re-warn) thousands of times. */
static uint64_t g_failed[64][2]; static int g_nfailed = 0;
static int dump_failed(uint64_t s, uint64_t e) {
    for (int i = 0; i < g_nfailed; i++) if (g_failed[i][0] == s && g_failed[i][1] == e) return 1;
    return 0;
}
static void record_failed(uint64_t s, uint64_t e) {
    if (dump_failed(s, e) || g_nfailed >= 64) return;
    g_failed[g_nfailed][0] = s; g_failed[g_nfailed][1] = e; g_nfailed++;
}

/* E9Patch maps its trampolines as thousands of separate VMAs, and the maps file is polled
 * often, so parsing every line with fgets()+sscanf() on every poll is too slow.  Read the
 * file in ONE read(), skip everything if the bytes are unchanged since the last poll (they
 * almost always are after start-up), and hand-parse the lines that do need parsing. */
static pthread_mutex_t g_maps_mtx = PTHREAD_MUTEX_INITIALIZER;
static char *g_maps_prev = NULL; static size_t g_maps_prev_len = 0;
static unsigned long long g_maps_polls = 0, g_maps_parses = 0;
static unsigned long long g_map_execs = 0, g_maps_forgotten = 0;

/* ---- stale mappings across execve() --------------------
 * execve() REPLACES the address space, so every mapping recorded before it is dead.
 * The map list is a union over time; spanning the whole traced lifetime, a traced command
 * that itself exec's (`-- taskset -c N <prog>', `-- setarch -R ...', `-- /bin/sh -c ...')
 * would produce a sideband holding TWO address spaces.  An image mapped in both -- a
 * rewritten libc.so.6 reached through LD_LIBRARY_PATH always is, because the wrapper links
 * against it too -- would then have two load bases, and `ptrecon' could pick the DEAD one.
 * Forget the old space at each exec stop. */
static void maps_forget_old_address_space(void) {
    g_map_execs++;
    if (g_nmaps) { g_maps_forgotten += g_nmaps; g_nmaps = 0;
                   if (g_hidx && g_hcap) memset(g_hidx, 0, g_hcap * sizeof *g_hidx); }
    /* the unchanged-bytes shortcut must not swallow the first poll of the new space */
    free(g_maps_prev); g_maps_prev = NULL; g_maps_prev_len = 0;
    g_nfailed = 0;   /* "this range could not be dumped" was about the old space's ranges */
}

static unsigned long hexval(const char **q) {
    unsigned long v = 0; const char *p = *q;
    for (;; p++) {
        int c = *p;
        if (c >= '0' && c <= '9') v = v*16 + (c - '0');
        else if (c >= 'a' && c <= 'f') v = v*16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = v*16 + (c - 'A' + 10);
        else break;
    }
    *q = p; return v;
}

static void poll_maps(pid_t pid) {
    char fn[64]; snprintf(fn, sizeof fn, "/proc/%d/maps", (int)pid);
    int mfd = open(fn, O_RDONLY); if (mfd < 0) return;
    static char *buf = NULL; static size_t cap = 0;
    size_t len = 0;
    for (;;) {
        if (len + 65536 > cap) { size_t nc = cap ? cap*2 : (1u<<20); char *nb = realloc(buf, nc+1); if (!nb) { close(mfd); return; } buf = nb; cap = nc; }
        ssize_t r = read(mfd, buf + len, cap - len);
        if (r <= 0) break;
        len += (size_t)r;
    }
    close(mfd);
    buf[len] = 0;
    g_maps_polls++;
    if (g_maps_prev && g_maps_prev_len == len && !memcmp(g_maps_prev, buf, len)) return;  /* nothing changed */
    { char *keep = realloc(g_maps_prev, len + 1);
      if (keep) { memcpy(keep, buf, len); keep[len] = 0; g_maps_prev = keep; g_maps_prev_len = len; } }
    g_maps_parses++;
    char *cursor = buf, *nl;
    char line[1024];
    for (; cursor && *cursor; cursor = nl ? nl + 1 : NULL) {
        nl = strchr(cursor, '\n');
        size_t ll = nl ? (size_t)(nl - cursor) : strlen(cursor);
        if (ll >= sizeof line) ll = sizeof line - 1;
        memcpy(line, cursor, ll); line[ll] = 0;
        {
        struct mapent m; memset(&m, 0, sizeof m); unsigned long s, e, off; char perms[8]; char path[512] = "";
        const char *q = line;
        s = hexval(&q); if (*q != '-') continue; q++;
        e = hexval(&q); if (*q != ' ') continue;
        while (*q == ' ') q++;
        { int k = 0; while (*q && *q != ' ' && k < 7) perms[k++] = *q++; perms[k] = 0; if (k < 4) continue; }
        while (*q == ' ') q++;
        off = hexval(&q);
        while (*q == ' ') q++;                 /* dev */
        while (*q && *q != ' ') q++;
        while (*q == ' ') q++;                 /* inode */
        while (*q && *q != ' ') q++;
        while (*q == ' ') q++;                 /* path (may be empty) */
        { int k = 0; while (*q && *q != '\n' && k < 511) path[k++] = *q++; path[k] = 0; }
        const int is_file = (path[0] == '/');
        const int is_exec = (perms[2] == 'x');
        if (!is_file && !is_exec) continue;          /* [heap]/[stack]/anon data: not needed to decode */
        /* A mapping is identified by (range, name, file offset).  E9Patch's loader
         * re-maps the text page of the very same file from a DIFFERENT file offset
         * (the patched copy) with MAP_FIXED, so an entry with the same range and a
         * different offset SUPERSEDES the earlier one -- it must replace it, not be
         * dropped as a duplicate and not be kept alongside it. */
        int dup = 0; long super = hidx_find(s, e, path);
        if (super >= 0 && g_maps[super].off == off) { dup = 1; super = -1; }
        if (dup) continue;
        m.start = s; m.end = e; m.off = off; strncpy(m.perms, perms, 7);
        strncpy(m.name, path, sizeof m.name - 1);   /* the kernel's own name for the mapping */
        if (is_file) strncpy(m.path, path, 511);
        else {
            /* anonymous executable code ([vdso], a JIT area, ...): dump the bytes.
             * [vsyscall] is a kernel-owned page that cannot be read out of the
             * process and is never really executed (it is emulated), so skip it. */
            if (!g_dumpdir || !strcmp(path, "[vsyscall]")) { record_failed(s, e); continue; }
            if (dump_failed(s, e)) continue;
            char tag[32];
            if (path[0] == '[') { size_t k = 0; for (const char *q = path + 1; *q && *q != ']' && k < sizeof tag - 1; q++) tag[k++] = *q; tag[k] = 0; }
            else snprintf(tag, sizeof tag, "anon%d", g_ndumps);
            snprintf(m.path, sizeof m.path, "%s.%s.bin", g_dumpdir, tag);
            if (dump_range(pid, s, e, m.path) != 0) {
                fprintf(stderr, "warning: cannot dump %s (%#lx-%#lx) -- the decoder will lose sync there\n", path, s, e);
                record_failed(s, e);
                continue;
            }
            g_ndumps++;
            m.off = 0;
        }
        if (super >= 0) g_maps[super] = m;   /* same range+name, new file offset: it SUPERSEDES */
        else maps_append(&m);
        }
    }
}
/* ---- the traced thread's TLS base ------------------------------------------
 * `%fs' has no architectural read instruction in user space, and PTracer's
 * analyzer anchors `fs_base' only ONCE per thread (at `main'), so a parallel
 * reconstruction chunk that starts mid-stream can never learn it from the value
 * log -- every `%fs:'-based address in that chunk is then unknown.
 * It is a process constant for a single-threaded
 * target, so read it here with ptrace at a stop (PTRACE_EVENT_EXIT is the last
 * one where the address space is still intact) and put it in the sideband.
 */
static unsigned long long g_fs_base = 0;
extern int g_want_switch;                 /* defined below (--no-switch) */
extern int g_cpu_wide;
/* Every thread of the traced process (PTRACE_O_TRACECLONE), with the `fs_base' read at its
 * first ptrace stop -- the kernel sets it from CLONE_SETTLS in copy_thread(), before the
 * thread ever runs, so the first stop already shows the final value; a 0 is re-tried at every
 * later stop and at the exit stop.  The main thread's is 0 until the loader's arch_prctl. */
struct thr { pid_t tid; unsigned long long fs_base; int exited; unsigned long long first_stop_ns;
             /* The spare TCB this thread receives at its first
              * stop, whether its creator's clone event has been seen, whether it is held stopped. */
             unsigned long long tcb; int seen_clone, stopped_once, held; };
static struct thr *g_thr = NULL; static size_t g_nthr = 0, g_capthr = 0;
static uint64_t g_nclone = 0, g_tcb_assigned = 0, g_tcb_none = 0, g_tcb_held = 0;
static struct thr *thr_get(pid_t tid) {
    for (size_t i = 0; i < g_nthr; i++) if (g_thr[i].tid == tid) return &g_thr[i];
    if (g_nthr == g_capthr) { size_t nc = g_capthr ? g_capthr * 2 : 64; struct thr *n = realloc(g_thr, nc * sizeof *n);
                              if (!n) { fprintf(stderr, "error: out of memory for the thread table\n"); exit(2); } g_thr = n; g_capthr = nc; }
    struct thr *t = &g_thr[g_nthr++]; memset(t, 0, sizeof *t); t->tid = tid;
    return t;
}
static void poll_fs_base_tid(pid_t tid, pid_t leader) {
    struct user_regs_struct r;
    struct iovec iov = { &r, sizeof r };
    if (ptrace(PTRACE_GETREGSET, tid, (void *)(long)NT_PRSTATUS, &iov)) return;
    if (!r.fs_base) return;
    thr_get(tid)->fs_base = (unsigned long long)r.fs_base;
    if (tid == leader) g_fs_base = (unsigned long long)r.fs_base;
}
/* ---- Hand every new thread its own %gs region BEFORE its first instruction --------
 * Linux copies the GS base at clone(2) and glibc blocks all signals around the clone, so the
 * Stage-2 runtime cannot give a new pthread its own TCB by a fault: it would log into its
 * CREATOR's cv/gt/count areas through the inherited base until the shim's wrapper ran.
 * The runtime therefore keeps a ring of SPARE, fully
 * initialised TCBs in its control block (runtime/rt/ptlog_abi.h); at the creator's
 * PTRACE_EVENT_CLONE we pop one through the creator's %gs base, and at the child's first stop --
 * it has not executed an instruction yet -- write the child's tid into it and make it the
 * child's GS base.  A child whose first stop arrives before its creator's clone event is HELD
 * until that event names its TCB.  Threads of a process without the runtime (gs_base 0, no
 * magic) are left alone. */
#ifndef PTRACE_ARCH_PRCTL
#define PTRACE_ARCH_PRCTL 30
#endif
#ifndef ARCH_SET_GS
#define ARCH_SET_GS 0x1001
#endif
static int tcb_peek(pid_t tid, unsigned long long addr, unsigned long long *v) {
    errno = 0; long r = ptrace(PTRACE_PEEKDATA, tid, (void *)addr, 0);
    if (r == -1 && errno) return -1;
    *v = (unsigned long long)r; return 0;
}
/* Pop a spare TCB through the (stopped) creator's %gs; 0 if none / no runtime. */
static unsigned long long tcb_take_spare(pid_t creator) {
    struct user_regs_struct r; struct iovec iov = { &r, sizeof r };
    if (ptrace(PTRACE_GETREGSET, creator, (void *)(long)NT_PRSTATUS, &iov)) return 0;
    unsigned long long gs = r.gs_base, magic = 0, abi = 0, ctl = 0, put = 0, get = 0, t = 0;
    if (!gs || (gs & 4095)) return 0;
    if (tcb_peek(creator, gs + PTLOG_TCB_MAGIC_OFF, &magic) || magic != PTLOG_MAGIC) return 0;
    if (tcb_peek(creator, gs + PTLOG_TCB_ABI_OFF, &abi) || abi < PTLOG_ABI) return 0;
    if (tcb_peek(creator, gs + PTLOG_TCB_CTL_OFF, &ctl) || !ctl) return 0;
    if (tcb_peek(creator, ctl + PTLOG_CTL_SPARE_PUT_OFF, &put) || tcb_peek(creator, ctl + PTLOG_CTL_SPARE_GET_OFF, &get)) return 0;
    if ((long long)(put - get) <= 0) { g_tcb_none++; return 0; }
    if (tcb_peek(creator, ctl + PTLOG_CTL_SPARE_ARR_OFF + 8ULL * (get % PTLOG_SPARE_MAX), &t) || !t) return 0;
    if (ptrace(PTRACE_POKEDATA, creator, (void *)(ctl + PTLOG_CTL_SPARE_GET_OFF), (void *)(get + 1))) return 0;
    return t;
}
/* The child is stopped and has not run: it owns `tcb' from here on. */
static void tcb_assign(pid_t child, unsigned long long tcb) {
    if (ptrace(PTRACE_POKEDATA, child, (void *)(tcb + PTLOG_TCB_TID_OFF), (void *)(long)child)) return;
    if (ptrace(PTRACE_ARCH_PRCTL, child, (void *)tcb, (void *)(long)ARCH_SET_GS)) { perror("ptrace ARCH_SET_GS"); return; }
    g_tcb_assigned++;
}

/* Per-CPU summary for the sideband (filled from the drain contexts after the join). */
struct cpu_out { int cpu; const char *aux; const char *sw; uint64_t aux_bytes, lost, trunc, n_sw, n_itrace, data_lost; };
static void write_sideband(const char *fn, pid_t pid, int status, size_t aux_bytes, int wrapped, long mtc_period, struct perf_event_mmap_page *pc,
                           const struct cpu_out *cpus, int ncpus) {
    /* Write to a temporary file and rename() it into place only after the
     * last byte is flushed.  A consumer therefore never sees a HALF-WRITTEN
     * sideband (e.g. a JSON cut off at a 4 KiB stdio boundary, which would read as
     * "no map list" instead of as an error).  If this process dies before the rename, `fn' simply does not
     * exist and the caller fails loudly. */
    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", fn) >= (int)sizeof tmp) { fprintf(stderr, "sideband: path too long\n"); return; }
    FILE *f = fopen(tmp, "w"); if (!f) { perror("sideband"); return; }
    unsigned eax=0, ebx=0, ecx=0, edx=0; __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    unsigned fam = ((eax >> 8) & 0xf) + ((eax >> 20) & 0xff), model = ((eax >> 4) & 0xf) | ((eax >> 12) & 0xf0), step = eax & 0xf;
    unsigned a15=0,b15=0,c15=0,d15=0; __get_cpuid_count(0x15, 0, &a15, &b15, &c15, &d15);
    fprintf(f, "{\"pid\":%d,\"exit\":%d,\"aux_bytes\":%zu,\"wrapped\":%d,\"mtc_period\":%ld,\n", (int)pid, WIFEXITED(status)?WEXITSTATUS(status):-1, aux_bytes, wrapped, mtc_period);
    fprintf(f, " \"cpu\":{\"family\":%u,\"model\":%u,\"stepping\":%u},\"cpuid15\":{\"eax\":%u,\"ebx\":%u},\n", fam, model, step, a15, b15);
    fprintf(f, " \"time\":{\"mult\":%u,\"shift\":%u,\"zero\":%llu,\"cap_user_time\":%u},\n", pc->time_mult, pc->time_shift, (unsigned long long)pc->time_zero, (unsigned)pc->cap_user_time);
    fprintf(f, " \"fs_base\":%llu,\n", g_fs_base);
    fprintf(f, " \"trace_gate\":{\"controlled\":%d,\"transitions\":%u,\"last_enabled_ns\":%llu,\"last_disabled_ns\":%llu},\n",
            g_trace_control_path != NULL, g_gate_transitions,
            (unsigned long long)g_gate_enabled_ns, (unsigned long long)g_gate_disabled_ns);
    /* ---- multi-threaded capture: additive keys ----------------
     * "sb_version": 2 says the two lists below exist.  `cpus' has one entry per --cpu event
     * (a per-task capture has none); `aux' is that core's AUX file and `switch_file' the
     * binary sw_rec stream drained from its data ring.  `threads' is every thread ptrace saw,
     * with the fs_base the kernel gave it; the main thread is the entry whose tid == pid. */
    fprintf(f, " \"sb_version\":2,\"capture_mode\":\"%s\",\"switch_records\":%d,\n", ncpus ? (g_cpu_wide ? "cpu-wide" : "per-task-per-cpu") : "per-task", g_want_switch && ncpus ? 1 : 0);
    fprintf(f, " \"cpus\":[");
    for (int i = 0; i < ncpus; i++)
        fprintf(f, "%s{\"cpu\":%d,\"aux\":\"%s\",\"aux_bytes\":%llu,\"lost_bytes\":%llu,\"truncated\":%llu,\"switch_file\":\"%s\",\"n_switch\":%llu,\"n_itrace_start\":%llu,\"data_lost\":%llu}",
                i ? "," : "", cpus[i].cpu, cpus[i].aux ? cpus[i].aux : "", (unsigned long long)cpus[i].aux_bytes,
                (unsigned long long)cpus[i].lost, (unsigned long long)cpus[i].trunc, cpus[i].sw ? cpus[i].sw : "",
                (unsigned long long)cpus[i].n_sw, (unsigned long long)cpus[i].n_itrace, (unsigned long long)cpus[i].data_lost);
    fprintf(f, "],\n \"threads\":[");
    { int first = 1;
      /* the leader first, whatever order the stops came in */
      for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < g_nthr; i++) {
            if ((pass == 0) != (g_thr[i].tid == pid)) continue;
            fprintf(f, "%s{\"tid\":%d,\"fs_base\":%llu}", first ? "" : ",", (int)g_thr[i].tid,
                    g_thr[i].tid == pid && !g_thr[i].fs_base ? g_fs_base : g_thr[i].fs_base);
            first = 0;
        }
      if (first) fprintf(f, "{\"tid\":%d,\"fs_base\":%llu}", (int)pid, g_fs_base); }
    fprintf(f, "],\n");
    /* The map list is a UNION OVER TIME, so a range the loader later re-mapped from a
     * different file offset can appear TWICE.  E9Patch does exactly that: its loader MAP_FIXEDs
     * sub-ranges of the image's own text from the patched copy, and the sub-range's end need not
     * equal the original mapping's end, so poll_maps()'s exact-range "supersede" rule does not
     * catch it.  libipt then has two sections for the same address and decodes the WRONG bytes
     * (`trace stream does not match query' resyncs).  Resolution: for EXECUTABLE mappings the NEWEST entry wins --
     * emit each one minus every range a later executable entry covers, splitting it if necessary.
     * Non-executable entries are left alone; nothing decodes them. */
    size_t emitted = 0, trimmed = 0;
    fprintf(f, " \"maps\":[\n");
    for (size_t i = 0; i < g_nmaps; i++) {
        struct mapent *m = &g_maps[i];
        int is_exec = (m->perms[2] == 'x');
        /* pieces of [start,end) still owned by this entry */
        unsigned long long pcs[64][2]; size_t np = 0;
        pcs[np][0] = m->start; pcs[np][1] = m->end; np = 1;
        if (is_exec) {
            for (size_t j = i + 1; j < g_nmaps && np; j++) {
                struct mapent *n = &g_maps[j];
                if (n->perms[2] != 'x') continue;
                if (n->end <= m->start || n->start >= m->end) continue;
                size_t out = 0; unsigned long long np2[64][2];
                for (size_t k = 0; k < np; k++) {
                    unsigned long long a = pcs[k][0], b = pcs[k][1];
                    /* `out' can outrun `k' when an earlier piece SPLIT in two, so this
                     * (previously unguarded) store could write np2[64] and smash the frame. */
                    if (out >= 64) break;
                    if (n->end <= a || n->start >= b) { np2[out][0]=a; np2[out][1]=b; out++; continue; }
                    if (n->start > a && out < 64) { np2[out][0]=a; np2[out][1]=n->start; out++; }
                    if (n->end   < b && out < 64) { np2[out][0]=n->end; np2[out][1]=b; out++; }
                }
                if (out != np || (out && (np2[0][0] != pcs[0][0] || np2[0][1] != pcs[0][1]))) trimmed++;
                np = out; for (size_t k = 0; k < out; k++) { pcs[k][0]=np2[k][0]; pcs[k][1]=np2[k][1]; }
            }
        }
        for (size_t k = 0; k < np; k++) {
            if (emitted++) fprintf(f, ",\n");
            fprintf(f, "  {\"start\":%llu,\"end\":%llu,\"offset\":%llu,\"perms\":\"%s\",\"path\":\"%s\",\"name\":\"%s\"}",
                    pcs[k][0], pcs[k][1], (unsigned long long)(m->off + (pcs[k][0] - m->start)),
                    m->perms, m->path, m->name);
        }
    }
    fprintf(f, "\n ]}\n");
    int werr = ferror(f);
    if (fflush(f) || werr) { fprintf(stderr, "sideband: write error, not installing %s\n", fn); fclose(f); return; }
    if (fclose(f)) { perror("sideband fclose"); return; }
    if (rename(tmp, fn)) { perror("sideband rename"); return; }
    if (trimmed) fprintf(stderr, "sideband: %zu executable mappings trimmed where a later MAP_FIXED superseded them\n", trimmed);
}


/* Uniform ToPA order: pick the largest page order k <= 10 (= MAX_PAGE_ORDER, 4 MB; a watermark below 4 MB costs one
 * PMI per 2^k pages, so k stays as large as memory allows) such that the free buddy blocks of order >= k, counted in order-k units,
 * cover the whole AUX allocation twice.  Returns k; warns when even order 0 would not fit. */
static int g_topa_order = -1;
static int topa_safe_order(unsigned long long total_bytes, int quiet) {
    unsigned long long avail[11] = {0};
    FILE *f = fopen("/proc/buddyinfo", "r");
    if (!f) { if (!quiet) fprintf(stderr, "aux: /proc/buddyinfo unreadable, chunk order 8 assumed\n"); return 8; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char zone[32]; int node, pos = 0;
        if (sscanf(line, "Node %d, zone %31s %n", &node, zone, &pos) < 2) continue;
        if (strcmp(zone, "Normal") && strcmp(zone, "DMA32")) continue;
        char *p = line + pos;
        for (int o = 0; o <= 10; o++) {
            char *e; unsigned long long c = strtoull(p, &e, 10);
            if (e == p) break;
            p = e;
            for (int k = 0; k <= o; k++) avail[k] += c << (o - k);   /* an order-o block = 2^(o-k) order-k blocks */
        }
    }
    fclose(f);
    for (int k = 10; k >= 0; k--) {
        unsigned long long need = (total_bytes >> 12 >> k) + 1;
        if (avail[k] >= 2 * need) return k;
    }
    if (!quiet) fprintf(stderr, "aux: WARNING free memory cannot hold the AUX rings twice even in 4 KB pages; "
                                "ToPA chunks may be mixed -- lower --aux-mb\n");
    return 0;
}

/* ---------------- draining AUX writer ------------
 *
 * The AUX area is mapped PROT_READ|PROT_WRITE, which puts the PT event in
 * perf's NON-overwrite mode: the kernel fills [aux_tail, aux_tail+aux_size)
 * and then simply STOPS.  If nobody advances aux_tail, `aux_head' saturates at
 * `aux_size' and every capture is a PREFIX.  So a reader thread drains the
 * ring while the child runs and publishes aux_tail, exactly as perf's own
 * auxtrace_mmap__read() does: aux_head/aux_tail are FREE-RUNNING 64-bit byte
 * counters, the buffer position is (counter % aux_size), so a window that
 * straddles the end of the ring is two memcpy()s.
 *
 * Draining straight into the file is unreliable:
 *
 *   (1)     A disk can be slower than the PT stream an instrumented program
 *           generates.  Once enough dirty pages accumulate the kernel throttles
 *           the writer to device speed, a drain pass stalls, the AUX ring fills
 *           during that stall, and perf_aux_output_begin() then DISABLES the
 *           event (PERF_AUX_FLAG_TRUNCATED).
 *   (2)     A re-enable issued only after that same slow write returns, and only
 *           when the truncation COUNT has grown, can lose the race against the
 *           kernel's pending irq_work disable, or come after the child exited;
 *           the event then stays off for the rest of the run.
 *
 * So the burst is absorbed in RAM instead:
 *
 *   - the drain thread only ever memcpy()s the ring into a chunked STAGING
 *     queue (~10 GB/s) and publishes aux_tail immediately;
 *   - a separate WRITER thread moves staging to the file at whatever rate the
 *     disk gives, and keeps going after the child has exited;
 *   - the event is re-enabled BEFORE the write, and a WATCHDOG re-issues
 *     PERF_EVENT_IOC_ENABLE on every pass in which aux_head did not advance,
 *     which is what closes the pending_disable race (enabling a running event
 *     is a no-op in the kernel, so this is free and idempotent).
 *
 * Loss accounting: if the producer ever gets more than aux_size ahead of us the
 * oldest bytes are gone -- counted in g_aux_lost.  The kernel says the same
 * thing independently through PERF_RECORD_AUX records in the *data* ring, whose
 * PERF_AUX_FLAG_TRUNCATED bit is counted in g_aux_trunc_recs; that ring is
 * drained here too so it cannot fill either.
 *
 * PSB alignment is deliberately NOT enforced: this is a byte-exact copy of the
 * stream, and chunked/parallel decoding finds its own PSBs in the saved file.
 */
#ifndef PERF_AUX_FLAG_TRUNCATED
#define PERF_AUX_FLAG_TRUNCATED 0x01
#endif
static double now_sec(void);
static volatile int g_drain_stop = 0;
static uint64_t g_aux_written = 0;         /* bytes handed to the staging queue */
static uint64_t g_aux_lost = 0;            /* bytes the producer overwrote before we read them */
static uint64_t g_aux_trunc_recs = 0;      /* PERF_RECORD_AUX with PERF_AUX_FLAG_TRUNCATED */
static uint64_t g_aux_max_fill = 0;        /* high-water mark of the ring occupancy */
static uint64_t g_data_lost_recs = 0;      /* PERF_RECORD_LOST* in the data ring */
static uint64_t g_aux_resumed = 0;         /* times the event was re-enabled after a truncation */
static uint64_t g_aux_fulllap = 0;        /* AUX records larger than half the ring */
static uint64_t g_aux_rearm = 0;           /* watchdog PERF_EVENT_IOC_ENABLEs (aux_head stalled) */
/* --trace-after S deliberately leaves the
 * PT event DISABLED for the first S seconds, so aux_head does not advance -- which is exactly the
 * condition the watchdog below exists to repair.  The watchdog (and the truncation-resume) must
 * not arm an event that was never armed, so both are gated on this flag, which only
 * pt_delay_enable() (or the trace-control thread) sets. */
static volatile int g_pt_armed = 1;        /* 0 while --trace-after is still counting down */
static pthread_mutex_t g_pt_control_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_control_stop = 0;
static double   g_aux_stall_s = 0;         /* seconds aux_head did not advance while the child ran */
static int g_zero_drained = 0;
static uint64_t g_zero_holes = 0, g_stale = 0;            /* --zero-drained: memset each drained ring region */
static FILE *g_auxlog = NULL;             /* --auxrec-log FILE */
static FILE *g_drainlog = NULL;            /* --drain-log FILE: one line per drain pass */

/* ---- staging queue: RAM between the AUX ring and the file ----------------
 *
 * The disk gives ~0.7-1.2 GB/s and instrumented CPython generates up to ~3.5 GB/s of PT, so the
 * difference has to sit in RAM for the length of the run.  Three roles, three threads:
 *
 *   drain thread   memcpy()s the AUX ring into the chunk it currently owns (ctx->cur).  It never
 *                  takes a lock for the copy and never allocates -- both used to be on this path
 *                  and both caused ring overflows: a fresh 32 MB malloc costs 8 192 first-touch
 *                  page faults, which at 3 GB/s is longer than the 256 MB ring's 85 ms of slack.
 *   writer thread  pops full chunks and fwrite()s them, then RECYCLES the chunk (its pages stay
 *                  faulted in for the rest of the run).
 *   allocator      keeps the free list topped up in the background, up to --stage-mb.
 *
 * A pool of --stage-prefault-mb is populated before the child is released, so the first burst
 * never allocates either.
 */
#define STAGE_CHUNK (32u * 1024u * 1024u)
struct stage_chunk { unsigned char *p; size_t len; struct stage_chunk *next; FILE *sink; };
static pthread_mutex_t g_stage_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_stage_more = PTHREAD_COND_INITIALIZER;   /* writer waits for a full chunk */
static pthread_cond_t  g_stage_room = PTHREAD_COND_INITIALIZER;   /* drainer waits for a free chunk */
static struct stage_chunk *g_sq_head = NULL, *g_sq_tail = NULL;   /* full, waiting to be written */
static struct stage_chunk *g_free_list = NULL;                    /* ready to be filled */
static size_t g_free_n = 0, g_alloc_bytes = 0, g_stage_bytes = 0, g_stage_peak = 0;
static size_t g_stage_max = 4096ull * 1024 * 1024;                /* --stage-mb */
static int    g_writer_stop = 0, g_alloc_stop = 0;
static FILE  *g_sink = NULL;                                      /* the (first) AUX file; per-ctx sinks in drain_ctx */
static uint64_t g_stage_waits = 0;         /* times the drainer had to wait for the writer */
static double   g_stage_wait_s = 0;
static uint64_t g_sink_bytes = 0;
static double   g_sink_s = 0;

static struct stage_chunk *chunk_new(void) {
    struct stage_chunk *n = malloc(sizeof *n);
    if (!n) return NULL;
    n->p = mmap(NULL, STAGE_CHUNK, PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    if (n->p == MAP_FAILED) { free(n); return NULL; }
    n->len = 0; n->next = NULL; n->sink = NULL;
    return n;
}

/* Take a chunk to fill.  Called only by the drain thread. */
static struct stage_chunk *stage_take(void) {
    for (;;) {
        pthread_mutex_lock(&g_stage_mtx);
        if (g_free_list) { struct stage_chunk *c = g_free_list; g_free_list = c->next; g_free_n--;
                           pthread_mutex_unlock(&g_stage_mtx); c->len = 0; c->next = NULL; return c; }
        if (g_alloc_bytes + STAGE_CHUNK <= g_stage_max) {
            g_alloc_bytes += STAGE_CHUNK;
            pthread_mutex_unlock(&g_stage_mtx);
            struct stage_chunk *c = chunk_new();
            if (c) return c;
            pthread_mutex_lock(&g_stage_mtx); g_alloc_bytes -= STAGE_CHUNK; pthread_mutex_unlock(&g_stage_mtx);
        } else pthread_mutex_unlock(&g_stage_mtx);
        /* the staging cap is reached: wait for the writer.  Never drop bytes. */
        pthread_mutex_lock(&g_stage_mtx);
        if (!g_free_list) { double w0 = now_sec(); g_stage_waits++;
                            pthread_cond_wait(&g_stage_room, &g_stage_mtx);
                            g_stage_wait_s += now_sec() - w0; }
        pthread_mutex_unlock(&g_stage_mtx);
    }
}

static void stage_push(struct stage_chunk *c) {
    pthread_mutex_lock(&g_stage_mtx);
    c->next = NULL;
    if (g_sq_tail) g_sq_tail->next = c; else g_sq_head = c;
    g_sq_tail = c;
    g_stage_bytes += c->len;
    if (g_stage_bytes > g_stage_peak) g_stage_peak = g_stage_bytes;
    pthread_cond_signal(&g_stage_more);
    pthread_mutex_unlock(&g_stage_mtx);
}

/* Append len bytes to the staging queue (called only by the owning drain thread).
 * `*cur' is that thread's partially filled chunk; the chunk remembers which AUX
 * file it belongs to, so N per-CPU drain threads share one writer. */
static void stage_append(struct stage_chunk **cur, FILE *sink, const unsigned char *src, size_t len) {
    while (len) {
        if (!*cur) { *cur = stage_take(); (*cur)->sink = sink; }
        struct stage_chunk *c = *cur;
        size_t room = STAGE_CHUNK - c->len, n = len < room ? len : room;
        memcpy(c->p + c->len, src, n);      /* no lock, no allocation */
        c->len += n; src += n; len -= n;
        if (c->len == STAGE_CHUNK) { stage_push(c); *cur = NULL; }
    }
}

/* The writer thread: staging -> file, at whatever rate the disk gives. */
static void *writer_thread(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_stage_mtx);
        while (!g_sq_head && !g_writer_stop) pthread_cond_wait(&g_stage_more, &g_stage_mtx);
        struct stage_chunk *c = g_sq_head;
        if (!c) { pthread_mutex_unlock(&g_stage_mtx); break; }
        g_sq_head = c->next; if (!g_sq_head) g_sq_tail = NULL;
        g_stage_bytes -= c->len;
        pthread_mutex_unlock(&g_stage_mtx);
        double w0 = now_sec();
        FILE *sk = c->sink ? c->sink : g_sink;
        if (sk && c->len && g_zero_drained) {  /* all-zero MB pieces become holes (sparse file) */
            static const unsigned char zmb[1u << 20];
            for (size_t o = 0; o < c->len; ) {
                size_t n = c->len - o < sizeof zmb ? c->len - o : sizeof zmb;
                if (n == sizeof zmb && !memcmp(c->p + o, zmb, n)) { if (fseeko(sk, (off_t)n, SEEK_CUR)) perror("aux-out seek"); g_zero_holes += n; }
                else if (fwrite(c->p + o, 1, n, sk) != n) perror("aux-out write");
                o += n;
            }
        } else if (sk && c->len && fwrite(c->p, 1, c->len, sk) != c->len) perror("aux-out write");
        g_sink_s += now_sec() - w0;
        g_sink_bytes += c->len;
        c->len = 0;
        pthread_mutex_lock(&g_stage_mtx);
        c->next = g_free_list; g_free_list = c; g_free_n++;   /* recycle: pages stay faulted in */
        pthread_cond_signal(&g_stage_room);
        pthread_mutex_unlock(&g_stage_mtx);
    }
    return NULL;
}

/* Keeps the free list topped up so the drain thread never has to mmap. */
static void *alloc_thread(void *arg) {
    size_t target = *(size_t *)arg;
    while (!g_alloc_stop) {
        int want;
        pthread_mutex_lock(&g_stage_mtx);
        want = (g_free_n < target && g_alloc_bytes + STAGE_CHUNK <= g_stage_max);
        if (want) g_alloc_bytes += STAGE_CHUNK;
        pthread_mutex_unlock(&g_stage_mtx);
        if (!want) { usleep(1000); continue; }
        struct stage_chunk *c = chunk_new();
        pthread_mutex_lock(&g_stage_mtx);
        if (c) { c->next = g_free_list; g_free_list = c; g_free_n++; pthread_cond_signal(&g_stage_room); }
        else g_alloc_bytes -= STAGE_CHUNK;
        pthread_mutex_unlock(&g_stage_mtx);
    }
    return NULL;
}

/* ---- thread attribution: context-switch records ------------------------------
 *
 * A per-CPU intel_pt event with `context_switch = 1' puts a PERF_RECORD_SWITCH_CPU_WIDE
 * record in the DATA ring at every switch on that core (and a PERF_RECORD_ITRACE_START
 * when the event starts), each carrying sample_id = { pid, tid, time, cpu } of the task
 * being switched in (or out).  With `exclude_kernel' the trace is off in ring 0, so a
 * core's AUX stream is a sequence of user regions [TIP.PGE, TIP.PGD] and a context switch
 * always lies in a gap between two of them: the region that starts at TIP.PGE time T
 * belongs to the LAST switch-in on that core with time <= T.  The record time is the perf
 * clock, which on this machine is derived from the TSC with exactly the parameters the
 * mmap page publishes (cap_user_time_zero: time_mult/time_shift/time_zero), so it is
 * converted back to the TSC domain of the PT packets here (perf's own perf_time_to_tsc),
 * and BOTH values are stored.  One binary file per core: `<sideband>.cpu<N>.sw'. */
struct sw_rec {
    uint64_t time;      /* perf clock (ns) */
    uint64_t tsc;       /* the same instant in TSC ticks */
    uint32_t pid, tid;  /* the task switched IN (SW_IN) or OUT (SW_OUT) */
    uint32_t flags;     /* SW_* below */
    uint32_t cpu;
};
#define SW_IN       0x1
#define SW_OUT      0x2
#define SW_PREEMPT  0x4     /* PERF_RECORD_MISC_SWITCH_OUT_PREEMPT */
#define SW_ITRACE   0x8     /* PERF_RECORD_ITRACE_START: the task current when tracing began */
#define SW_LOST     0x10    /* a PERF_RECORD_LOST preceded this one: records are missing */
#ifndef PERF_RECORD_MISC_SWITCH_OUT_PREEMPT
#define PERF_RECORD_MISC_SWITCH_OUT_PREEMPT (1 << 14)
#endif
int g_want_switch = 1;                    /* --no-switch turns the records off */
int g_cpu_wide = 0;                       /* --cpu-wide (sideband capture_mode) */

/* --trace-after S: enable every PT event S seconds after the child was released (see the
 * comment at attr.enable_on_exec).  A thread, not an alarm, so that it cannot interrupt the
 * ptrace waitpid loop. */
void *pt_delay_enable(void *arg);
void *pt_delay_enable(void *arg)
{
    struct { long *fds; int n; double after; } *d = arg;   /* fds is long[] (main) */
    struct timespec ts;
    ts.tv_sec = (time_t)d->after;
    ts.tv_nsec = (long)((d->after - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    pthread_mutex_lock(&g_pt_control_mtx);
    for (int i = 0; i < d->n; i++) ioctl((int)d->fds[i], PERF_EVENT_IOC_ENABLE, 0);
    g_pt_armed = 1;                        /* The watchdog may re-arm from here on */
    pthread_mutex_unlock(&g_pt_control_mtx);
    fprintf(stderr, "trace enabled after %.2f s (--trace-after)\n", d->after);
    return NULL;
}
static uint64_t perf_time_to_tsc(const struct perf_event_mmap_page *pc, uint64_t t) {
    if (!pc->cap_user_time_zero || !pc->time_mult) return 0;
    uint64_t d = t - pc->time_zero;
    uint64_t quot = d / pc->time_mult, rem = d % pc->time_mult;
    return (quot << pc->time_shift) + (rem << pc->time_shift) / pc->time_mult;
}

/* A bounded workload-window capture, not a full-process trace. The caller owns
 * a one-byte regular control file: '0' disables, '1' enables. Polling happens on
 * the collector's cores. Serialize disable against watchdog re-enables. */
struct trace_control { long *fds; int n; int fd; };
static void *trace_control_thread(void *arg) {
    struct trace_control *c = arg;
    char previous = '0';
    while (!__atomic_load_n(&g_control_stop, __ATOMIC_ACQUIRE)) {
        char value;
        if (pread(c->fd, &value, 1, 0) == 1 && (value == '0' || value == '1') && value != previous) {
            pthread_mutex_lock(&g_pt_control_mtx);
            g_pt_armed = 0;
            for (int i = 0; i < c->n; ++i) {
                if (ioctl((int)c->fds[i], value == '1' ? PERF_EVENT_IOC_ENABLE : PERF_EVENT_IOC_DISABLE, 0)) {
                    perror("trace-control ioctl"); _exit(74);
                }
            }
            g_pt_armed = value == '1';
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t ns = (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
            if (value == '1') g_gate_enabled_ns = ns; else g_gate_disabled_ns = ns;
            g_gate_transitions++;
            fprintf(stderr, "trace-control state=%c ns=%llu\n", value, (unsigned long long)ns);
            pthread_mutex_unlock(&g_pt_control_mtx);
            previous = value;
        }
        struct timespec delay = {0, 1000000}; nanosleep(&delay, NULL);
    }
    return NULL;
}

struct drain_ctx {
    struct perf_event_mmap_page *pc;
    void *aux; size_t aux_len;
    uint64_t *phash; uint64_t stale;   /* --zero-drained: per-ring-page content hash, stale bytes staged as zeros */
    unsigned char *data; size_t data_len;
    int staging;                 /* 1 = copy the ring into the staging queue */
    int fd;
    volatile int *child_alive;   /* the watchdog only re-arms while the child runs */
    /* per-event state (one drain_ctx per --cpu) */
    int cpu;                     /* -1 = per-task */
    FILE *sink;                  /* this event's AUX file */
    struct stage_chunk *cur;     /* this drain thread's partially filled chunk */
    FILE *swf;                   /* switch records (NULL = not recording) */
    uint64_t aux_written, aux_lost, aux_trunc_recs, aux_max_fill, data_lost_recs, aux_resumed, aux_rearm;
    double aux_stall_s;
    uint64_t n_sw, n_itrace, n_sw_other;   /* switch records written / ITRACE_START / unparsed record types */
    int lost_pending;            /* a PERF_RECORD_LOST was seen: flag the next switch record */
    pthread_t th;
};

/* One pass over the data ring: count PERF_RECORD_AUX truncations, then release it. */
static void drain_data_ring(struct drain_ctx *c) {
    if (!c->data || !c->data_len) return;
    uint64_t head = __atomic_load_n(&c->pc->data_head, __ATOMIC_ACQUIRE);
    uint64_t tail = c->pc->data_tail;
    while (tail < head) {
        struct perf_event_header hdr;
        size_t off = (size_t)(tail % c->data_len);
        for (size_t k = 0; k < sizeof hdr; k++) ((unsigned char *)&hdr)[k] = c->data[(off + k) % c->data_len];
        if (hdr.size < sizeof hdr || tail + hdr.size > head) break;
        if (hdr.type == PERF_RECORD_AUX && hdr.size >= sizeof hdr + 24) {
            uint64_t flags = 0;
            size_t fo = off + sizeof hdr + 16;
            for (size_t k = 0; k < 8; k++) ((unsigned char *)&flags)[k] = c->data[(fo + k) % c->data_len];
            if (flags & PERF_AUX_FLAG_TRUNCATED) { g_aux_trunc_recs++; c->aux_trunc_recs++; }
            {   /* Mixed-ToPA signature: one AUX record claiming more than half the ring (impossible with
                 * watermark PMIs unless the kernel mis-programmed the ToPA offset) */
                uint64_t as = 0; for (size_t k = 0; k < 8; k++) ((unsigned char *)&as)[k] = c->data[(off + sizeof hdr + 8 + k) % c->data_len];
                if (as > c->aux_len / 2) g_aux_fulllap++;
            }
            if (g_auxlog) {   /* Every PERF_RECORD_AUX (offset, size, flags, sample_id) */
                unsigned char rec[96]; size_t n = hdr.size < sizeof rec ? hdr.size : sizeof rec;
                for (size_t k = 0; k < n; k++) rec[k] = c->data[(off + k) % c->data_len];
                uint64_t ao = 0, as = 0; uint32_t spid = 0, stid = 0; uint64_t stime = 0;
                memcpy(&ao, rec + sizeof hdr, 8); memcpy(&as, rec + sizeof hdr + 8, 8);
                if (n >= sizeof hdr + 24 + 16) { memcpy(&spid, rec + sizeof hdr + 24, 4); memcpy(&stid, rec + sizeof hdr + 28, 4); memcpy(&stime, rec + sizeof hdr + 32, 8); }
                fprintf(g_auxlog, "%.6f cpu=%d t=%llu tid=%u off=%llu size=%llu flags=0x%llx tail=%llu%s\n", now_sec(), c->cpu,
                        (unsigned long long)stime, stid, (unsigned long long)ao, (unsigned long long)as, (unsigned long long)flags,
                        (unsigned long long)c->pc->aux_tail, as >= c->aux_len / 2 ? " BIG" : "");
            }
        } else if (hdr.type == PERF_RECORD_LOST || hdr.type == PERF_RECORD_LOST_SAMPLES) {
            g_data_lost_recs++; c->data_lost_recs++; c->lost_pending = 1;
        } else if (c->swf && (hdr.type == PERF_RECORD_SWITCH_CPU_WIDE || hdr.type == PERF_RECORD_ITRACE_START
                              || hdr.type == PERF_RECORD_SWITCH)) {
            /* layout: header | [u32 pid, u32 tid  (ITRACE_START) | u32 next_prev_pid, u32
             * next_prev_tid (SWITCH_CPU_WIDE) | nothing (SWITCH)] | sample_id = u32 pid, u32
             * tid, u64 time, u32 cpu, u32 res  (sample_type TID|TIME|CPU, sample_id_all). */
            unsigned char rec[128]; size_t n = hdr.size < sizeof rec ? hdr.size : sizeof rec;
            for (size_t k = 0; k < n; k++) rec[k] = c->data[(off + k) % c->data_len];
            size_t body = (hdr.type == PERF_RECORD_SWITCH) ? 0 : 8;
            size_t sid = sizeof hdr + body;               /* start of sample_id */
            if (sid + 24 <= hdr.size && sid + 24 <= n) {
                struct sw_rec r; memset(&r, 0, sizeof r);
                uint32_t spid, stid, scpu; uint64_t stime;
                memcpy(&spid, rec + sid, 4); memcpy(&stid, rec + sid + 4, 4);
                memcpy(&stime, rec + sid + 8, 8); memcpy(&scpu, rec + sid + 16, 4);
                r.time = stime; r.tsc = perf_time_to_tsc(c->pc, stime);
                r.pid = spid; r.tid = stid; r.cpu = scpu;
                if (hdr.type == PERF_RECORD_ITRACE_START) { r.flags = SW_ITRACE | SW_IN; c->n_itrace++; }
                else {
                    r.flags = (hdr.misc & PERF_RECORD_MISC_SWITCH_OUT) ? SW_OUT : SW_IN;
                    if (hdr.misc & PERF_RECORD_MISC_SWITCH_OUT_PREEMPT) r.flags |= SW_PREEMPT;
                }
                if (c->lost_pending) { r.flags |= SW_LOST; c->lost_pending = 0; }
                fwrite(&r, sizeof r, 1, c->swf); c->n_sw++;
            } else c->n_sw_other++;
        }
        tail += hdr.size;
    }
    __atomic_store_n(&c->pc->data_tail, tail, __ATOMIC_RELEASE);
}

/* --zero-drained: the kernel sometimes advances aux_head over ring bytes the hardware did not
 * write this lap (never-written zeros followed by an exact copy of an older lap's
 * data, same TSCs).  Every full 4 KB ring page carries the hash of the content drained from it last
 * time; a page that comes back byte-identical is STALE and is staged as zeros (PAD), so the decoder
 * never replays an old lap as new trace.  Partial pages are passed through. */
static inline uint64_t page_hash(const unsigned char *p) {
    const uint64_t *w = (const uint64_t *)p; uint64_t h = 0x243F6A8885A308D3ull;
    for (int i = 0; i < 512; i++) { h ^= w[i]; h *= 0x9E3779B97F4A7C15ull; h ^= h >> 29; }
    return h | 1;
}
static void stage_ring(struct drain_ctx *c, size_t off, size_t len) {
    static const unsigned char zpage[4096];
    const unsigned char *base = c->aux;
    if (!g_zero_drained || !c->phash) { stage_append(&c->cur, c->sink, base + off, len); return; }
    while (len) {
        size_t pg_off = off & 4095, n = 4096 - pg_off; if (n > len) n = len;
        if (pg_off == 0 && n == 4096) {
            uint64_t h = page_hash(base + off); size_t pg = off >> 12;
            if (c->phash[pg] == h) { stage_append(&c->cur, c->sink, zpage, 4096); c->stale += 4096; g_stale += 4096; }
            else { c->phash[pg] = h; stage_append(&c->cur, c->sink, base + off, 4096); }
        } else stage_append(&c->cur, c->sink, base + off, n);
        off += n; len -= n;
    }
}

/* One pass over the AUX ring: copy everything the kernel produced into the
 * staging queue and publish aux_tail.  No I/O happens here (see the draining writer above). */
static size_t drain_aux_once(struct drain_ctx *c) {
    uint64_t head = __atomic_load_n(&c->pc->aux_head, __ATOMIC_ACQUIRE);
    uint64_t tail = c->pc->aux_tail;
    if (head == tail) return 0;
    uint64_t avail = head - tail;
    if (avail > c->aux_len) {                 /* we fell behind: the oldest bytes are gone */
        g_aux_lost += avail - c->aux_len; c->aux_lost += avail - c->aux_len;
        tail = head - c->aux_len; avail = c->aux_len;
    }
    if (avail > g_aux_max_fill) g_aux_max_fill = avail;
    if (avail > c->aux_max_fill) c->aux_max_fill = avail;
    size_t t_off = (size_t)(tail % c->aux_len), h_off = (size_t)(head % c->aux_len);
    if (c->staging) {
        if (h_off > t_off) {
            stage_ring(c, t_off, (size_t)avail);
        } else {                              /* wraps around the end of the ring */
            stage_ring(c, t_off, c->aux_len - t_off);
            if (h_off) stage_ring(c, 0, h_off);
        }
    }
    g_aux_written += avail; c->aux_written += avail;
    __atomic_store_n(&c->pc->aux_tail, head, __ATOMIC_RELEASE);
    return (size_t)avail;
}

/* The reader BLOCKS on the perf fd and is woken by the kernel when aux_watermark bytes have
 * accumulated (plus a 20 ms timeout as a safety net and as the watchdog tick).  Spinning on
 * usleep() instead costs the traced process a preemption every poll, and every preemption is a
 * PT disable/enable pair: measured on PolyBench gemm_mini (a 1.25 MB trace in a 256 MB ring,
 * which needs no draining at all), a 100 us spin cost one decoder resync and up to 40 000
 * instructions of trace per run.  Blocking on the watermark means a capture that fits in the ring
 * is drained exactly ONCE, after the child exits, so draining is free for short runs.
 *
 * Two things happen on every pass besides the copy:
 *   - if the data ring reported a truncation, the event is re-enabled AT ONCE (the copy above is
 *     a memcpy, so "at once" is now microseconds rather than the ~430 ms a stalled fwrite cost);
 *   - if aux_head did not advance since the previous pass and the child is still running, the
 *     event is re-enabled anyway.  perf_event_enable() on a running event returns immediately, so
 *     this costs nothing and it is the only thing that reliably beats the kernel's asynchronous
 *     pending_disable: whichever ENABLE lands after the irq_work wins, and we keep issuing them. */
static void *drain_thread(void *arg) {
    struct drain_ctx *c = arg;
    uint64_t seen_trunc = 0, last_head = 0;
    double last_progress = now_sec();
    struct pollfd pfd; pfd.fd = c->fd; pfd.events = POLLIN;
    while (!g_drain_stop) {
        /* 2 ms, not 20: with --child-core the traced process has a core to itself, so waking
         * the drain thread often no longer preempts it (that was the reason for the long
         * timeout), and a missed watermark wakeup on a loaded machine now costs 2 ms of ring
         * fill instead of 20. */
        if (c->fd >= 0) { pfd.revents = 0; poll(&pfd, 1, 2); }
        else usleep(2000);
        double t_pass = now_sec();
        uint64_t head0 = __atomic_load_n(&c->pc->aux_head, __ATOMIC_ACQUIRE);
        size_t n = drain_aux_once(c);
        drain_data_ring(c);
        int trunc = 0, rearm = 0;
        pthread_mutex_lock(&g_pt_control_mtx);
        if (c->aux_trunc_recs > seen_trunc) {          /* the ring filled: the kernel disabled us */
            seen_trunc = c->aux_trunc_recs;
            trunc = 1;
            if (g_pt_armed && c->fd >= 0 && ioctl(c->fd, PERF_EVENT_IOC_ENABLE, 0) == 0) { g_aux_resumed++; c->aux_resumed++; }
        }
        if (head0 != last_head) { last_head = head0; last_progress = t_pass; }
        else if (g_pt_armed && c->child_alive && *c->child_alive && t_pass - last_progress >= 0.010) {
            /* watchdog: no PT bytes for 10 ms while the child runs.  A benchmark that is merely
             * blocked also lands here, and that is fine: PERF_EVENT_IOC_ENABLE on an event that
             * is not OFF returns without doing anything, so the only case in which this has an
             * effect is the one it exists for -- an event the kernel disabled behind our back. */
            g_aux_stall_s += t_pass - last_progress; c->aux_stall_s += t_pass - last_progress; last_progress = t_pass;
            if (c->fd >= 0 && ioctl(c->fd, PERF_EVENT_IOC_ENABLE, 0) == 0) { g_aux_rearm++; c->aux_rearm++; }
            rearm = 1;
        }
        pthread_mutex_unlock(&g_pt_control_mtx);
        if (g_drainlog)
            fprintf(g_drainlog, "%.6f cpu=%d head=%llu drained=%zu stage=%zu trunc=%d rearm=%d\n",
                    t_pass, c->cpu, (unsigned long long)head0, n, g_stage_bytes, trunc, rearm);
    }
    /* final pass: the event is already disabled by the time the flag is set */
    drain_aux_once(c);
    drain_data_ring(c);
    if (c->cur && c->cur->len) { stage_push(c->cur); c->cur = NULL; }   /* the partial last chunk */
    return NULL;
}

#define PMU_PATH "/sys/bus/event_source/devices/intel_pt"

static long perf_event_open(struct perf_event_attr *a, pid_t pid, int cpu,
                            int group_fd, unsigned long flags) {
    return syscall(__NR_perf_event_open, a, pid, cpu, group_fd, flags);
}

static int read_sysfs_int(const char *path, long *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = fscanf(f, "%ld", out);
    fclose(f);
    return n == 1 ? 0 : -1;
}

/* Parse a format file like "config:12" or "config:14-17"; return low bit and
 * (optionally) the field width in bits. */
static int read_format_bits(const char *name, int *lowbit, int *width) {
    char path[256];
    snprintf(path, sizeof(path), PMU_PATH "/format/%s", name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int lo = -1, hi = -1;
    int n = fscanf(f, "config:%d-%d", &lo, &hi);
    fclose(f);
    if (n < 1) return -1;
    if (lowbit) *lowbit = lo;
    if (width)  *width = (n == 2) ? (hi - lo + 1) : 1;
    return 0;
}

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Periodic /proc/PID/maps snapshots, off the ptrace loop's critical path.  Dense for
 * `burst_ms' after the exec stop (that is when the loader dlopen()s and unmaps things), then
 * every `every_ms'; an unchanged maps file costs one read() and a memcmp. */
struct mp_arg_t { pid_t pid; volatile int *alive; volatile int *execed; double burst_ms, every_ms; int gate; };
/* --map-poll-gate (default off): after the burst, a steady-state poll first reads
 * /proc/PID/status (O(1) in the kernel: mm counters, no VMA walk) and re-reads /proc/PID/maps
 * only when VmExe/VmLib/VmSize changed since the last full poll.  Reading the maps file of a
 * whole-process image (tens of thousands of VMAs, mostly E9Patch trampoline maps) at every poll
 * slows the traced program down.  Executable-mapping changes always
 * move VmExe/VmLib (exec_vm); VmSize catches the rest.  Blind spot: an unmap +
 * map of the SAME size inside one --map-poll-ms period leaves all three counters unchanged. */
static unsigned long long g_gate_skips = 0;
static int vm_sig(pid_t pid, char *out, size_t n) {
    char fn[64], buf[4096]; snprintf(fn, sizeof fn, "/proc/%d/status", (int)pid);
    int fd = open(fn, O_RDONLY); if (fd < 0) return -1;
    ssize_t r = read(fd, buf, sizeof buf - 1); close(fd); if (r <= 0) return -1; buf[r] = 0;
    size_t k = 0; out[0] = 0;
    for (char *l = buf; l && *l; ) { char *e = strchr(l, '\n'); if (e) *e = 0;
        if (!strncmp(l, "VmSize:", 7) || !strncmp(l, "VmExe:", 6) || !strncmp(l, "VmLib:", 6)) {
            size_t m = strlen(l); if (k + m + 1 < n) { memcpy(out + k, l, m); k += m; out[k++] = '|'; out[k] = 0; } }
        l = e ? e + 1 : NULL; }
    return 0;
}
static void *map_poll_thread(void *arg) {
    struct mp_arg_t *a = arg;
    double t0 = 0;
    while (*a->alive) {
        /* Nothing is sampled before the exec stop: pt_capture2's own pre-exec image must never
         * end up in the sideband (the reconstructor depends on that). */
        if (!*a->execed) { usleep(200); continue; }
        if (t0 == 0) t0 = now_sec();
        double now = now_sec();
        int burst = (now - t0) * 1000.0 < a->burst_ms;
        double every = burst ? 1.0 : a->every_ms;
        static char last_sig[512]; char sig[512];
        if (a->gate && !burst && vm_sig(a->pid, sig, sizeof sig) == 0 && last_sig[0] && !strcmp(sig, last_sig)) {
            g_gate_skips++; usleep((useconds_t)(every * 1000)); continue; }
        if (a->gate && vm_sig(a->pid, sig, sizeof sig) == 0) memcpy(last_sig, sig, sizeof sig);
        pthread_mutex_lock(&g_maps_mtx); poll_maps(a->pid); pthread_mutex_unlock(&g_maps_mtx);
        usleep((useconds_t)(every * 1000));
    }
    return NULL;
}

/* ---- The TRACED run's environment must not be pt_capture2's own -----
 *
 * A `--gt-all' build is run with
 * `LD_LIBRARY_PATH=<rewritten libc>' and `PTLOG_GT=1'.  When those are exported
 * around the whole command line -- which is the obvious way to write the script
 * -- *pt_capture2 itself* is dynamically linked
 * against the REWRITTEN libc.so.6 and starts recording its own memory accesses
 * into a ground-truth ring.  Two consequences, both silent:
 *   * every libc call in the tracer goes through an E9Patch trampoline, so the
 *     parent runs ~100x slower and writes its own multi-GB gt file;
 *   * when the tracer's own ring reaches PTLOG_GT_MAX the runtime "flushes and
 *     exits" -- `_exit()' from inside whatever the tracer was doing.  Hitting that
 *     inside write_sideband() leaves the JSON cut off at a 4 KiB stdio boundary;
 *     hitting it earlier loses the AUX file as well.  It only shows up when the CHILD exits early, because
 *     only a small PTLOG_GT_MAX is reached by the tracer's own trickle of accesses.
 *
 * So the tracee-only variables are QUARANTINED here: they are removed from our own
 * environment and handed to the child through `--child-env', after which this
 * process re-execs itself so that the ordinary system libc is loaded.  The child's
 * environment is bit-for-bit what the caller asked for; only the tracer's changes.
 * Gated on PTLOG_GT so that no other kind of run (a buffer-sink timing cell, say)
 * changes behaviour.  `--child-env' can also be used directly; the re-exec is the
 * fallback for callers that export the variables themselves.
 */
static const char *const TRACEE_ONLY_ENV[] = {
    "LD_LIBRARY_PATH", "LD_PRELOAD",
    "PTLOG_GT", "PTLOG_GT_DIR", "PTLOG_GT_WIN", "PTLOG_GT_MAX",
    "PTLOG_DIR", "PTLOG_SYNC", "PTLOG_STATS", NULL
};
static void quarantine_tracee_env(int argc, char **argv) {
    if (!getenv("PTLOG_GT")) return;                  /* not a --gt-all run: nothing to do */
    if (getenv("PT_CAPTURE2_CLEANENV")) return;       /* already re-exec'd: never loop */
    /* Collect "VAR=VALUE" for every tracee-only variable that is actually set. */
    char *pass[32]; int npass = 0;
    for (int k = 0; TRACEE_ONLY_ENV[k] && npass < 30; k++) {
        const char *v = getenv(TRACEE_ONLY_ENV[k]);
        if (!v) continue;
        size_t n = strlen(TRACEE_ONLY_ENV[k]) + strlen(v) + 2;
        char *s = malloc(n); if (!s) return;
        snprintf(s, n, "%s=%s", TRACEE_ONLY_ENV[k], v);
        pass[npass++] = s;
    }
    if (!npass) return;
    /* argv[0] --child-env V=X ... <original args> */
    char **na = calloc((size_t)argc + 2 * (size_t)npass + 2, sizeof *na);
    if (!na) return;
    int n = 0; na[n++] = argv[0];
    for (int k = 0; k < npass; k++) { na[n++] = (char *)"--child-env"; na[n++] = pass[k]; }
    for (int k = 1; k < argc; k++) na[n++] = argv[k];
    na[n] = NULL;
    for (int k = 0; TRACEE_ONLY_ENV[k]; k++) unsetenv(TRACEE_ONLY_ENV[k]);
    setenv("PT_CAPTURE2_CLEANENV", "1", 1);
    fprintf(stderr, "pt_capture2: PTLOG_GT is set in MY environment -- re-execing without the "
                    "tracee-only variables; the child still gets all %d of them\n", npass);
    execv("/proc/self/exe", na);
    /* Only reached if the re-exec failed: say so loudly, the run is compromised. */
    perror("pt_capture2: re-exec (/proc/self/exe)");
    fprintf(stderr, "pt_capture2: WARNING -- continuing with the tracee's environment; if "
                    "LD_LIBRARY_PATH names a --gt-all libc this process WILL exit early and "
                    "truncate its own output\n");
}

int main(int argc, char **argv) {
    { const char *sw = getenv("PT_CAPTURE2_SWALLOW_SIGTRAP"); g_swallow_trap = sw && atoi(sw) > 0; }

    long aux_mb = 128;
    long mtc_period = 3;       /* MTC frequency selector; tuned empirically */
    int want_ptw = 0;          /* --ptw: record PTWRITE (PTW) packets too */
    int no_ptw = 0;            /* --no-ptw (or PT_CAPTURE_NO_PTW=1): never set PTWEn, even with --ptw --
                                * simulates a PT CPU without PTWRITE */
    int no_branch = 0;         /* --no-branch: drop control-flow (TNT/TIP) packets */
    char *timeline = NULL;     /* --timeline FILE: dump each OVF's TSC */
    char *ptwwin = NULL;       /* --ptw-window FILE: per-window PTW packet counts */
    long ptwwin_us = 1000;     /* --ptw-window-us: window size in microseconds (default 1ms) */
    char *ptwdump = NULL;      /* --ptw-dump FILE: dump each PTW packet's payload value (one per line) */
    int no_decode = 0;         /* --no-decode: skip the offline packet scan (overhead runs) */
    int no_drain = 0;          /* --no-drain: fill the ring once and stop (prefix capture) */
    char *aux_out = NULL;      /* --aux-out FILE: raw AUX bytes for offline reconstruction */
    char *sideband = NULL;     /* --sideband FILE: JSON sideband (maps, cpu, clock) */
    char *drainlog = NULL;     /* --drain-log FILE: one line per drain pass (diagnostics) */
    long stage_mb = 4096;      /* --stage-mb: RAM between the AUX ring and the file */
    long prefault_mb = 1024;   /* --stage-prefault-mb: pool populated before the child is released */
    long wm_mb = 0;            /* --watermark-mb: AUX wakeup threshold (default: uniform ToPA order) */
    long wm_kb = 0;            /* --watermark-kb: the same in KB */
    int child_core = -1;       /* --child-core N: run ONLY the traced child on core N */
    double map_poll_ms = 20;   /* --map-poll-ms: steady-state /proc/PID/maps period, 0 = only at the
                                * exec and exit stops */
    double map_burst_ms = 500; int map_poll_gate = 0; /* --map-poll-burst-ms: poll at 1 kHz for this long after exec */
    /* --cpu N (REPEATABLE): per-CPU trace of core N (captures ALL threads on it, needed for
     * multi-threaded/JIT workloads).  Several --cpu open one event per core, pin the child to
     * exactly that set and write one AUX file per core.  None = per-task (the one thread). */
    #define MAX_CPUS 64
    int cpus[MAX_CPUS]; int ncpu = 0;
    /* --cpu-wide: whole-core events (pid = -1): EVERY task that runs on the core lands in
     * its AUX stream.  The default is per-TASK-per-CPU: (pid = child, cpu = N) with
     * `inherit', which the child's threads inherit at clone(2), so the PMU is only programmed while
     * one of the child's threads is on the core -- no other tenant's code is ever in the stream.
     * (libipt has no image for a foreign tenant, errors, and re-synchronises at the next PSB,
     * losing everything up to it.)  Inherited events write into the parent
     * event's buffers (perf_output_begin/perf_aux_output_begin redirect to the parent). */
    int cpu_wide = 0;
    double trace_after = 0;    /* --trace-after S: keep the PT event DISABLED for S seconds */
    long data_pages = 0;       /* --data-pages N: data ring pages (power of two; default 8, or 1024 with switch records) */
    char *child_env[32]; int n_child_env = 0;   /* --child-env VAR=VALUE (repeatable):
                                * set in the CHILD only, so the tracer never loads the tracee's
                                * (possibly rewritten) libc or arms its ground-truth ring. */
    quarantine_tracee_env(argc, argv);          /* may re-exec; see the comment above */
    int i = 1;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--aux-mb") && i+1 < argc) aux_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--mtc-period") && i+1 < argc) mtc_period = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ptw")) want_ptw = 1;
        else if (!strcmp(argv[i], "--no-ptw")) no_ptw = 1;
        else if (!strcmp(argv[i], "--no-branch")) no_branch = 1;
        else if (!strcmp(argv[i], "--no-decode")) no_decode = 1;
        else if (!strcmp(argv[i], "--no-drain")) no_drain = 1;
        else if (!strcmp(argv[i], "--no-switch")) g_want_switch = 0;
        else if (!strcmp(argv[i], "--timeline") && i+1 < argc) timeline = argv[++i];
        else if (!strcmp(argv[i], "--ptw-window") && i+1 < argc) ptwwin = argv[++i];
        else if (!strcmp(argv[i], "--ptw-window-us") && i+1 < argc) ptwwin_us = atol(argv[++i]);
        else if (!strcmp(argv[i], "--ptw-dump") && i+1 < argc) ptwdump = argv[++i];
        else if (!strcmp(argv[i], "--cpu") && i+1 < argc) {
            int c = atoi(argv[++i]); int dup = 0;
            for (int k = 0; k < ncpu; k++) if (cpus[k] == c) dup = 1;
            if (!dup) { if (ncpu >= MAX_CPUS) { fprintf(stderr, "too many --cpu\n"); return 2; } cpus[ncpu++] = c; }
        }
        else if (!strcmp(argv[i], "--data-pages") && i+1 < argc) data_pages = atol(argv[++i]);
        else if (!strcmp(argv[i], "--cpu-wide")) cpu_wide = 1;
        else if (!strcmp(argv[i], "--trace-after") && i+1 < argc) trace_after = atof(argv[++i]);
        else if (!strcmp(argv[i], "--trace-control") && i+1 < argc) g_trace_control_path = argv[++i];
        else if (!strcmp(argv[i], "--aux-out") && i+1 < argc) aux_out = argv[++i];
        else if (!strcmp(argv[i], "--sideband") && i+1 < argc) sideband = argv[++i];
        else if (!strcmp(argv[i], "--drain-log") && i+1 < argc) drainlog = argv[++i];
        else if (!strcmp(argv[i], "--auxrec-log") && i+1 < argc) { g_auxlog = fopen(argv[++i], "w"); if (g_auxlog) setvbuf(g_auxlog, NULL, _IOFBF, 1 << 20); }
        else if (!strcmp(argv[i], "--zero-drained")) g_zero_drained = 1;
        else if (!strcmp(argv[i], "--stage-mb") && i+1 < argc) stage_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--stage-prefault-mb") && i+1 < argc) prefault_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watermark-mb") && i+1 < argc) wm_mb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watermark-kb") && i+1 < argc) wm_kb = atol(argv[++i]);
        else if (!strcmp(argv[i], "--child-core") && i+1 < argc) child_core = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map-poll-ms") && i+1 < argc) map_poll_ms = atof(argv[++i]);
        else if (!strcmp(argv[i], "--map-poll-burst-ms") && i+1 < argc) map_burst_ms = atof(argv[++i]);
        else if (!strcmp(argv[i], "--map-poll-gate")) map_poll_gate = 1;
        else if (!strcmp(argv[i], "--child-env") && i+1 < argc) {
            if (n_child_env < 32) child_env[n_child_env++] = argv[++i];
            else { fprintf(stderr, "too many --child-env\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--scan")) {}
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else { fprintf(stderr, "unknown arg: %s\n", argv[i]); return 2; }
    }
    if (i >= argc) { fprintf(stderr, "no command (use -- <cmd>)\n"); return 2; }
    int control_fd = -1;
    if (g_trace_control_path) {
        struct stat st; char initial;
        control_fd = open(g_trace_control_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (trace_after > 0 || control_fd < 0 || fstat(control_fd, &st) || !S_ISREG(st.st_mode) ||
            pread(control_fd, &initial, 1, 0) != 1 || initial != '0') {
            fprintf(stderr, "--trace-control needs an existing regular file starting with 0, and no --trace-after\n");
            return 2;
        }
    }
    char **cmd = &argv[i];
    const int per_cpu = ncpu ? cpus[0] : -1;      /* the first core, for the single-event paths */
    g_cpu_wide = cpu_wide;
    const int nev = ncpu ? ncpu : 1;              /* number of perf events */
    if (!ncpu) g_want_switch = 0;                 /* a per-task event follows one thread: nothing to attribute */
    if (data_pages <= 0) data_pages = g_want_switch ? 1024 : 8;
    { long pw = 1; while (pw < data_pages) pw <<= 1; data_pages = pw; }

    /* 1. PMU type + config bit positions. */
    long pmu_type;
    if (read_sysfs_int(PMU_PATH "/type", &pmu_type)) {
        fprintf(stderr, "error: intel_pt PMU not present\n"); return 2;
    }
    int b_pt=-1, b_branch=-1, b_tsc=-1, b_mtc=-1, b_mtcp=-1, mtcp_w=1, b_ptw=-1;
    read_format_bits("pt", &b_pt, NULL);
    read_format_bits("branch", &b_branch, NULL);
    read_format_bits("tsc", &b_tsc, NULL);
    read_format_bits("mtc", &b_mtc, NULL);
    read_format_bits("mtc_period", &b_mtcp, &mtcp_w);
    read_format_bits("ptw", &b_ptw, NULL);
    if (b_pt < 0) { fprintf(stderr, "error: no pt config bit\n"); return 2; }
    /* NOPTW: a PT-capable CPU without PTWRITE (CPUID.(14H,0):EBX[4] = 0,
     * e.g. Ice Lake Xeon) rejects PTWEn, so --ptw is dropped there with a note instead of failing.
     * Images rewritten with --sync-carrier tnt execute no PTWRITE and need no PTW packets. */
    { const char *e = getenv("PT_CAPTURE_NO_PTW"); if (e && *e && *e != '0') no_ptw = 1; }
    if (want_ptw && !no_ptw) {
        unsigned a14 = 0, b14 = 0, c14 = 0, d14 = 0;
        if (!__get_cpuid_count(0x14, 0, &a14, &b14, &c14, &d14) || !(b14 & (1u << 4))) {
            fprintf(stderr, "note: this CPU has no PTWRITE (CPUID.14H:EBX[4] = 0): --ptw ignored\n");
            no_ptw = 1;
        }
    }
    if (want_ptw && no_ptw) { fprintf(stderr, "note: PTW packets disabled (PTWEn = 0)\n"); want_ptw = 0; }
    if (want_ptw && b_ptw < 0) { fprintf(stderr, "error: PMU has no ptw config bit\n"); return 2; }

    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    attr.type = pmu_type;
    attr.config = (1ULL << b_pt);
    if (b_branch >= 0 && !no_branch) attr.config |= (1ULL << b_branch);   /* control flow */
    if (b_tsc >= 0)    attr.config |= (1ULL << b_tsc);      /* wall-clock anchor */
    if (b_mtc >= 0)    attr.config |= (1ULL << b_mtc);      /* fine timing */
    if (b_mtcp >= 0)   attr.config |= ((uint64_t)mtc_period << b_mtcp);
    if (want_ptw)      attr.config |= (1ULL << b_ptw);          /* record PTW packets */
    /* Wake the draining reader when --watermark-mb has accumulated (default an EIGHTH of the ring:
     * the copy out of the ring is a memcpy now, so waking more often is nearly free and it keeps
     * the ring emptier, which is the headroom that absorbs a disk stall -- see drain_thread). */
    /* Mixed ToPA chunk orders: the kernel sizes the AUX buffer's high-order chunks by the
     * WATERMARK (rb_alloc_aux: max_order = get_order(aux_watermark), capped at MAX_PAGE_ORDER = 4 MB)
     * and falls back to smaller orders when the buddy allocator has no block of that order left.  A
     * fragmented machine therefore yields MIXED ToPA region sizes (4 MB, 4 MB, 2 MB, 4 MB ...), and a
     * 4 MB region then starts at a 2 MB-odd buffer offset.  intel_pt's pt_buffer_reset_offsets()
     * computes the in-region write offset as `head & (region_size - 1)', i.e. it assumes every region
     * is naturally aligned: on every (re)start inside such a region the hardware is programmed 2 MB
     * off.  Half the time the kernel then reports aux_head advancing by 2 MB + w (2 MB of stale ring
     * bytes), the other half by RING - 2 MB + w: a whole stale lap, after which only ~2 MB of the
     * ring is free, the hardware hits STOP within milliseconds and real trace is lost -- the "stale-AUX
     * storm".
     * Fix: keep the chunk order UNIFORM by choosing the watermark's order from /proc/buddyinfo so that
     * the free blocks of that order cover every ring with 2x headroom; uniform chunks are aligned by
     * construction.  --watermark-mb/-kb still override (then the check only warns). */
    { unsigned long long total = (unsigned long long)aux_mb * 1024 * 1024 * (unsigned long long)(ncpu ? ncpu : 1);
      unsigned long long wb = 0;
      int k = topa_safe_order(total, (wm_mb > 0 || wm_kb > 0) ? 1 : 0);
      if (wm_kb > 0) wb = (unsigned long long)wm_kb * 1024;
      else if (wm_mb > 0) wb = (unsigned long long)wm_mb * 1024 * 1024;
      else if (k >= 10) { long w = aux_mb / 8; if (w < 4) w = 4; wb = (unsigned long long)w * 1024 * 1024; }  /* 4 MB chunks are safe */
      else wb = 4096ULL << k;                                         /* smaller uniform chunks: watermark = one chunk */
      if (wb > (unsigned long long)aux_mb * 1024 * 1024 / 2) wb = (unsigned long long)aux_mb * 1024 * 1024 / 2;
      if (wb < 4096) wb = 4096;
      attr.aux_watermark = (uint32_t)(wb > 0xf0000000ULL ? 0xf0000000ULL : wb);
      int eff = 0; while (eff < 10 && (4096ULL << eff) < (unsigned long long)attr.aux_watermark) eff++;   /* = get_order() */
      g_topa_order = eff;
      fprintf(stderr, "aux: %ld MB x%d, watermark %llu KB -> AUX chunk order %d (%llu KB); buddyinfo-safe order %d%s\n",
              aux_mb, ncpu ? ncpu : 1, (unsigned long long)attr.aux_watermark >> 10, eff, (4096ULL << eff) >> 10, k,
              eff < 6 ? " -- WARNING: little free memory in large blocks, so the watermark (one PMI per chunk) is small and PT costs more; lower --aux-mb for timing runs" :
              eff > k ? " -- WARNING: the forced watermark asks for larger chunks than free memory can supply uniformly; mixed chunks trigger the kernel's misplaced-offset bug" : ""); }
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.disabled = 1;
    /* enable_on_exec is a per-task property; in cpu-wide mode we enable manually. */
    attr.enable_on_exec = (per_cpu < 0 || !cpu_wide) ? 1 : 0;
    /* --trace-after S: a SERVER cannot be traced from
     * its first instruction the way a batch job can -- it needs a start-up and a warm-up first,
     * and tracing the warm-up of a whole-program instrumented service costs gigabytes of AUX
     * that are then decoded for nothing.  Keep the event
     * disabled and enable it S seconds after the child is released; the window is then
     * [S, child exit], which the caller ends by killing the service. */
    if (trace_after > 0 || g_trace_control_path) { attr.enable_on_exec = 0; g_pt_armed = 0; }
    if (per_cpu >= 0 && !cpu_wide) attr.inherit = 1;      /* the child's threads inherit the event */
    if (g_want_switch) {
        /* Thread attribution (see struct sw_rec): every non-sample record in the data ring
         * carries { pid, tid, time, cpu } and a switch record is written at every context
         * switch on the core.  sample_type on an intel_pt event only affects the data ring. */
        attr.sample_type = PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CPU;
        attr.sample_id_all = 1;
        attr.context_switch = 1;
    }

    /* --child-core N: the traced child gets core N to ITSELF.  pt_capture2's own two
     * threads (the map poller and the drain thread) are moved OFF it -- sharing the core with
     * the benchmark would both slow the benchmark down and starve the drain thread.
     * With several --cpu the whole set is the child's and the parent leaves all of them. */
    {
        cpu_set_t leave; CPU_ZERO(&leave); int nleave = 0;
        if (child_core >= 0) { CPU_SET(child_core, &leave); nleave++; }
        for (int k = 0; k < ncpu; k++) if (!CPU_ISSET(cpus[k], &leave)) { CPU_SET(cpus[k], &leave); nleave++; }
        cpu_set_t cur; CPU_ZERO(&cur);
        if (nleave && (child_core >= 0 || ncpu > 1) && sched_getaffinity(0, sizeof cur, &cur) == 0) {
            cpu_set_t self = cur; int touched = 0;
            for (int k = 0; k < CPU_SETSIZE; k++) if (CPU_ISSET(k, &leave) && CPU_ISSET(k, &self)) { CPU_CLR(k, &self); touched = 1; }
            if (touched) {
                if (CPU_COUNT(&self) == 0) {            /* pinned to those cores alone: take the rest */
                    CPU_ZERO(&self);
                    /* Prefer the first 8 logical CPUs: on hybrid parts they are typically the
                     * P-cores, and the drain thread has to memcpy several GB/s out of the ring. */
                    for (int k = 0; k < 8; k++) if (!CPU_ISSET(k, &leave)) CPU_SET(k, &self);
                    if (CPU_COUNT(&self) == 0)
                        for (int k = 0; k < CPU_SETSIZE && k < 64; k++) if (!CPU_ISSET(k, &leave)) CPU_SET(k, &self);
                }
                if (sched_setaffinity(0, sizeof self, &self)) perror("parent sched_setaffinity");
            }
        }
    }

    /* 2. fork; child waits on a pipe, then execs the benchmark. */
    int pipefd[2];
    if (pipe(pipefd)) { perror("pipe"); return 2; }
    pid_t child = fork();
    if (child < 0) { perror("fork"); return 2; }
    if (child == 0) {
        close(pipefd[1]);
        /* In per-CPU mode, pin this process to the traced core(s) so all its
         * threads' execution lands in those cores' PT buffers. */
        cpu_set_t set; CPU_ZERO(&set); int np = 0;
        if (ncpu) { for (int k = 0; k < ncpu; k++) { CPU_SET(cpus[k], &set); np++; } }
        else if (child_core >= 0) { CPU_SET(child_core, &set); np++; }
        if (np && sched_setaffinity(0, sizeof(set), &set)) perror("child sched_setaffinity");
        char c; (void)!read(pipefd[0], &c, 1);   /* block until parent ready */
        close(pipefd[0]);
        /* The tracee-only variables, applied HERE and nowhere else. */
        for (int k = 0; k < n_child_env; k++)
            if (putenv(child_env[k])) perror("child putenv");
        execvp(cmd[0], cmd);
        perror("execvp"); _exit(127);
    }
    close(pipefd[0]);

    /* per-task: (pid=child, cpu=-1) follows the one thread. per-CPU: (pid=-1,
     * cpu=N) captures everything on core N -- required for multi-threaded/JIT. */
    static volatile int child_alive = 1;
    long fds[MAX_CPUS]; void *bases[MAX_CPUS]; void *auxs[MAX_CPUS]; struct perf_event_mmap_page *pcs[MAX_CPUS];
    long page = sysconf(_SC_PAGESIZE);
    size_t base_len = (size_t)(1 + data_pages) * (size_t)page;    /* control page + the data ring */
    size_t aux_len = (size_t)aux_mb * 1024 * 1024;   /* power-of-two MB => ok */
    for (int e = 0; e < nev; e++) {
        long fd = (per_cpu < 0) ? perf_event_open(&attr, child, -1, -1, 0)
                                : perf_event_open(&attr, cpu_wide ? -1 : child, cpus[e], -1, 0);
        if (fd < 0) {
            fprintf(stderr, "error: perf_event_open%s: %s\n", ncpu ? " (per-cpu)" : "", strerror(errno));
            kill_child:
            kill(child, SIGKILL); waitpid(child, NULL, 0);
            return 2;
        }
        void *base = mmap(NULL, base_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base == MAP_FAILED) { fprintf(stderr, "mmap base: %s\n", strerror(errno)); goto kill_child; }
        struct perf_event_mmap_page *pc = base;
        pc->aux_offset = base_len;
        pc->aux_size = aux_len;
        void *aux = mmap(NULL, aux_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, pc->aux_offset);
        if (aux == MAP_FAILED) { fprintf(stderr, "mmap aux (%ld MB): %s\n", aux_mb, strerror(errno)); goto kill_child; }
        fds[e] = fd; bases[e] = base; auxs[e] = aux; pcs[e] = pc;
    }
    struct perf_event_mmap_page *pc = pcs[0]; void *aux = auxs[0];

    /* Per-event output names: one AUX file per core (`<aux-out>.cpu<N>' when there is more
     * than one core, so a single-core capture keeps its exact old file name), and one switch
     * file per core next to the sideband. */
    char *aux_names[MAX_CPUS]; char *sw_names[MAX_CPUS];
    for (int e = 0; e < nev; e++) {
        aux_names[e] = NULL; sw_names[e] = NULL;
        if (aux_out) {
            /* /dev/null is a sink, not a filename prefix. Appending .cpuN
             * creates regular files under /dev and turns a multi-core timing
             * run into a disk/tmpfs-writing experiment (possibly ENOSPC). */
            if (nev == 1 || !strcmp(aux_out, "/dev/null")) aux_names[e] = strdup(aux_out);
            else { size_t n = strlen(aux_out) + 16; aux_names[e] = malloc(n); snprintf(aux_names[e], n, "%s.cpu%d", aux_out, cpus[e]); }
        }
        if (sideband && g_want_switch) { size_t n = strlen(sideband) + 24; sw_names[e] = malloc(n); snprintf(sw_names[e], n, "%s.cpu%d.sw", sideband, cpus[e]); }
    }

    /* The draining writer: with --aux-out, a reader thread copies
     * the ring into the file and publishes aux_tail while the child runs, so the
     * capture is complete however long the run is.  --no-drain gives a
     * fill-once-and-stop (prefix) capture. */
    static struct drain_ctx dctx[MAX_CPUS]; memset(dctx, 0, sizeof dctx);
    pthread_t wth, ath; int draining = 0, allocating = 0;
    static size_t alloc_target = 24;      /* chunks the allocator tries to keep free (768 MB) */
    for (int e = 0; e < nev; e++) {
        struct drain_ctx *d = &dctx[e];
        d->pc = pcs[e]; d->aux = auxs[e]; d->aux_len = aux_len;
        d->data = (unsigned char *)bases[e] + page; d->data_len = base_len - page;
        d->fd = (int)fds[e]; d->child_alive = &child_alive; d->cpu = ncpu ? cpus[e] : -1;
        if (sw_names[e]) { d->swf = fopen(sw_names[e], "wb"); if (!d->swf) perror("switch file"); else setvbuf(d->swf, NULL, _IOFBF, 1 << 20); }
    }
    if (aux_out && !no_drain) {
        g_stage_max = (size_t)stage_mb * 1024 * 1024;
        for (int e = 0; e < nev; e++) {
            dctx[e].staging = 1;
            if (g_zero_drained) dctx[e].phash = calloc(dctx[e].aux_len / 4096 + 1, sizeof(uint64_t));
            dctx[e].sink = fopen(aux_names[e], "wb");
            if (!dctx[e].sink) { perror("aux-out"); goto kill_child; }
            setvbuf(dctx[e].sink, NULL, _IONBF, 0);            /* the staging queue is the buffer */
        }
        g_sink = dctx[0].sink;
        if (drainlog) { g_drainlog = fopen(drainlog, "w"); if (g_drainlog) setvbuf(g_drainlog, NULL, _IOLBF, 0); }
        /* Populate the pool BEFORE the child is released, so the first burst never faults. */
        for (long k = 0; k < prefault_mb / (long)(STAGE_CHUNK >> 20); k++) {
            struct stage_chunk *n = chunk_new();
            if (!n) break;
            n->next = g_free_list; g_free_list = n; g_free_n++; g_alloc_bytes += STAGE_CHUNK;
        }
        if (pthread_create(&wth, NULL, writer_thread, NULL)) { perror("pthread_create (writer)"); goto kill_child; }
        if (!pthread_create(&ath, NULL, alloc_thread, &alloc_target)) allocating = 1;
        for (int e = 0; e < nev; e++)
            if (pthread_create(&dctx[e].th, NULL, drain_thread, &dctx[e])) { perror("pthread_create (drain)"); goto kill_child; }
        draining = 1;
    } else if (g_want_switch) {
        /* No AUX draining, but the switch records still have to be pulled out of the data ring
         * while the child runs (it is small): run the drain threads with staging off. */
        for (int e = 0; e < nev; e++)
            if (pthread_create(&dctx[e].th, NULL, drain_thread, &dctx[e])) { perror("pthread_create (drain)"); goto kill_child; }
        draining = 2;
    }

    if (sideband) { g_dumpdir = sideband;
        /* PTRACE_O_TRACECLONE: every thread the child creates is auto-attached and stops once
         * before it runs -- where its fs_base is read. */
        if (ptrace(PTRACE_SEIZE, child, 0, PTRACE_O_TRACEEXIT | PTRACE_O_TRACEEXEC | PTRACE_O_TRACECLONE)) perror("ptrace seize (sideband maps unavailable)"); }
    /* 3. Release the child; per-task trace auto-enables on exec, per-CPU must be
     * enabled manually here (just before the child starts running on core N). */
    if (per_cpu >= 0 && cpu_wide && trace_after <= 0 && !g_trace_control_path) for (int e = 0; e < nev; e++) ioctl(fds[e], PERF_EVENT_IOC_ENABLE, 0);
    struct trace_control control = {fds, nev, control_fd};
    pthread_t control_th;
    if (g_trace_control_path && pthread_create(&control_th, NULL, trace_control_thread, &control)) {
        perror("trace-control thread"); goto kill_child;
    }
    static struct { long *fds; int n; double after; } g_delay;
    pthread_t delay_th; int have_delay_th = 0;
    if (trace_after > 0) {
        g_delay.fds = fds; g_delay.n = nev; g_delay.after = trace_after;
        g_pt_armed = 0;                    /* No watchdog re-arm before the delay fires */
        extern void *pt_delay_enable(void *);
        if (pthread_create(&delay_th, NULL, pt_delay_enable, &g_delay)) perror("pthread_create (trace-after)");
        else have_delay_th = 1;
    }
    (void)have_delay_th;
    double t0 = now_sec();
    (void)!write(pipefd[1], "g", 1);
    close(pipefd[1]);
    int status;
    if (sideband) {
        /* The child was PTRACE_SEIZEd with PTRACE_O_TRACEEXEC|PTRACE_O_TRACEEXIT before release.
         * It stops once just after execve() -- that is where the FIRST snapshot is taken, so that
         * nothing from pt_capture2's own pre-exec image ends up in the sideband -- then it is
         * polled while running (to catch mappings unmapped before exit), and once more at the
         * exit stop, where the address space is still intact. */
        /* Every read of /proc/PID/maps takes the child's mmap_lock for reading, and CPython's
         * obmalloc mmap()s and munmap()s arenas constantly (which needs it for WRITING), so a
         * high-frequency poller from another core starves the benchmark.
         * The union-over-time only exists to catch mappings the loader unmaps before the exit
         * stop, and those all happen during start-up, so poll densely for --map-poll-burst-ms
         * after the exec stop and then every --map-poll-ms. */
        static volatile int execed = 0;
        /* The main loop BLOCKS in waitpid: an E9Patch-rewritten program can take thousands of
         * SIGILL stops (E9Patch's own evicted-instruction handler), and the child is
         * frozen from the moment it stops until the tracer restarts it, so a polling
         * (WNOHANG + sleep) loop would freeze it on every stop.  Periodic map polling runs in
         * its own thread.
         * MULTI-THREADED: with PTRACE_O_TRACECLONE every thread reports here, so the loop waits
         * for ANY tracee (__WALL) and ends when the thread-group LEADER is reaped -- which the
         * kernel only reports once every other thread is gone. */
        struct mp_arg_t mpa = { child, &child_alive, &execed, map_burst_ms, map_poll_ms, map_poll_gate };
        pthread_t mth; int mpolling = 0;
        if (map_poll_ms > 0 && !pthread_create(&mth, NULL, map_poll_thread, &mpa)) mpolling = 1;
        thr_get(child);
        for (;;) {
            pid_t r = waitpid(-1, &status, __WALL);
            if (r < 0) { if (errno == EINTR) continue; break; }
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                if (r == child) break;
                thr_get(r)->exited = 1;                       /* a non-leader thread is gone */
                continue;
            }
            if (WIFSTOPPED(status)) {
                int ev = status >> 16, sig = WSTOPSIG(status);
                g_nstops++; if (ev == 0 && sig == SIGILL) g_nsigill++;
                struct thr *t = thr_get(r);
                int first = !t->stopped_once; t->stopped_once = 1;
                if (!t->fs_base) poll_fs_base_tid(r, child);   /* cheap; 0 until the loader's arch_prctl */
                if (ev == PTRACE_EVENT_EXEC) { execed = 1; pthread_mutex_lock(&g_maps_mtx);
                                              maps_forget_old_address_space();
                                              poll_maps(child); pthread_mutex_unlock(&g_maps_mtx); }
                else if (ev == PTRACE_EVENT_CLONE) {
                    unsigned long nt = 0; g_nclone++;
                    if (!ptrace(PTRACE_GETEVENTMSG, r, 0, &nt) && nt) {
                        /* The new thread's TCB comes from ITS CREATOR's spare ring. */
                        struct thr *c = thr_get((pid_t)nt);
                        c->seen_clone = 1; c->tcb = tcb_take_spare(r);
                        if (c->held) {                          /* its first stop came first */
                            if (c->tcb) tcb_assign((pid_t)nt, c->tcb);
                            c->held = 0; ptrace(PTRACE_CONT, (pid_t)nt, 0, 0);
                        }
                    }
                }
                else if (ev == PTRACE_EVENT_EXIT) { if (r == child) { pthread_mutex_lock(&g_maps_mtx); poll_maps(child); pthread_mutex_unlock(&g_maps_mtx); } poll_fs_base_tid(r, child); }
                if (first && r != child && ev != PTRACE_EVENT_EXIT) {
                    /* A new thread's initial stop, before its first instruction. */
                    if (!t->seen_clone) { t->held = 1; g_tcb_held++; continue; }   /* wait for the creator's clone event */
                    if (t->tcb) tcb_assign(r, t->tcb);
                }
                /* A SIGTRAP stop that is NOT a ptrace event
                 * is a real signal the tracee raised -- the `int3' the HotSpot JIT patcher plants
                 * during its cross-modifying install (runtime/jit/java/jvmtiagent.cc) -- and it MUST
                 * be delivered: suppressing it would resume the thread one byte into the detour's rel32.
                 * Under PTRACE_SEIZE every tracer-generated stop carries an event number (ev != 0;
                 * group-stops and the auto-attach stop are PTRACE_EVENT_STOP), so ev == 0 with
                 * sig == SIGTRAP is always a genuine signal. */
                if (ev == 0 && sig == SIGTRAP) g_nsigtrap++;
                /* PT_CAPTURE2_SWALLOW_SIGTRAP=1 suppresses it (diagnostic only; the JVMTI agent's
                 * VM_INIT self-test must then abort the JVM with exit 72). */
                ptrace(PTRACE_CONT, r, 0, (ev != 0 || (sig == SIGTRAP && g_swallow_trap)) ? 0 : sig);
            }
        }
        child_alive = 0;
        if (mpolling) pthread_join(mth, NULL);
        fprintf(stderr, "ptrace: %llu stops (%llu SIGILL, %llu SIGTRAP %s), %llu map polls (%llu gate-skipped), %llu of them parsed, %zu threads (%llu clone events); "
                "spare TCBs assigned to %llu new threads (%llu had none available, %llu held for their clone event)\n",
                (unsigned long long)g_nstops, (unsigned long long)g_nsigill, (unsigned long long)g_nsigtrap,
                g_swallow_trap ? "SWALLOWED (PT_CAPTURE2_SWALLOW_SIGTRAP)" : "forwarded",
                g_maps_polls, g_gate_skips, g_maps_parses, g_nthr, (unsigned long long)g_nclone,
                (unsigned long long)g_tcb_assigned, (unsigned long long)g_tcb_none, (unsigned long long)g_tcb_held);
        if (!execed) fprintf(stderr, "warning: never saw the exec stop; the sideband maps may be incomplete\n");
        if (g_map_execs > 1)
            fprintf(stderr, "sideband: the traced command exec'd %llu times; %llu mappings of the "
                            "superseded address space(s) were forgotten.  The trace "
                            "before the LAST exec decodes against maps that no longer exist -- pin "
                            "with --child-core instead of a `taskset' prefix to avoid it.\n",
                    (unsigned long long)g_map_execs, (unsigned long long)g_maps_forgotten);
    } else waitpid(child, &status, 0);
    if (g_trace_control_path) {
        __atomic_store_n(&g_control_stop, 1, __ATOMIC_RELEASE);
        pthread_join(control_th, NULL);
        close(control_fd);
    }
    double wall = now_sec() - t0;
    pthread_mutex_lock(&g_pt_control_mtx);
    g_pt_armed = 0;
    for (int e = 0; e < nev; e++) ioctl(fds[e], PERF_EVENT_IOC_DISABLE, 0);
    pthread_mutex_unlock(&g_pt_control_mtx);

    size_t nbytes; int wrapped;
    void *scan_base = aux; size_t scan_map_len = 0;
    struct cpu_out couts[MAX_CPUS]; memset(couts, 0, sizeof couts);
    if (draining) {
        child_alive = 0;
        g_drain_stop = 1;
        for (int e = 0; e < nev; e++) pthread_join(dctx[e].th, NULL);
        if (draining == 1) {
            g_alloc_stop = 1; if (allocating) pthread_join(ath, NULL);
            pthread_mutex_lock(&g_stage_mtx); g_writer_stop = 1;
            pthread_cond_broadcast(&g_stage_more); pthread_mutex_unlock(&g_stage_mtx);
            pthread_join(wth, NULL);
            for (int e = 0; e < nev; e++) { fflush(dctx[e].sink); if (g_zero_drained && ftruncate(fileno(dctx[e].sink), ftello(dctx[e].sink))) perror("aux-out truncate"); fclose(dctx[e].sink); dctx[e].sink = NULL; }
            if (g_zero_drained) { fprintf(stderr, "zero-drained: %llu bytes of all-zero MB pieces written as holes; %llu STALE re-reported ring bytes staged as zeros\n", (unsigned long long)g_zero_holes, (unsigned long long)g_stale);
                for (int e = 0; e < nev; e++) fprintf(stderr, "  cpu %d: stale=%llu\n", dctx[e].cpu, (unsigned long long)dctx[e].stale); }
            g_sink = NULL;
        }
        for (int e = 0; e < nev; e++) if (dctx[e].swf) { fclose(dctx[e].swf); dctx[e].swf = NULL; }
        if (g_drainlog) { fclose(g_drainlog); g_drainlog = NULL; }
        if (g_auxlog) { fclose(g_auxlog); g_auxlog = NULL; }
        nbytes = (size_t)g_aux_written;
        wrapped = (g_aux_lost != 0 || g_aux_trunc_recs != 0);
        for (int e = 0; e < nev; e++) {
            couts[e].cpu = dctx[e].cpu; couts[e].aux = aux_names[e]; couts[e].sw = sw_names[e];
            couts[e].aux_bytes = dctx[e].aux_written; couts[e].lost = dctx[e].aux_lost; couts[e].trunc = dctx[e].aux_trunc_recs;
            couts[e].n_sw = dctx[e].n_sw; couts[e].n_itrace = dctx[e].n_itrace; couts[e].data_lost = dctx[e].data_lost_recs;
        }
        if (draining == 1) {
        fprintf(stderr, "child exit=%d wall=%.4fs aux_bytes=%zu (drained; ring=%ldMB x%d, peak fill %.1f%%, lost=%llu bytes, PERF_RECORD_AUX truncated=%llu)\n",
                WIFEXITED(status)?WEXITSTATUS(status):-1, wall, nbytes, aux_mb, nev,
                aux_len ? 100.0 * (double)g_aux_max_fill / (double)aux_len : 0.0,
                (unsigned long long)g_aux_lost, (unsigned long long)g_aux_trunc_recs);
        fprintf(stderr, "sink: %.0f MB/s while writing (%.0f MB/s over the run), staging peak %.0f MB of %ld, "
                        "waits=%llu (%.3fs), watchdog re-arms=%llu, aux_head stalled %.3fs\n",
                g_sink_s > 0 ? (double)g_sink_bytes / 1e6 / g_sink_s : 0.0,
                wall > 0 ? (double)nbytes / 1e6 / wall : 0.0,
                (double)g_stage_peak / 1e6, stage_mb,
                (unsigned long long)g_stage_waits, g_stage_wait_s,
                (unsigned long long)g_aux_rearm, g_aux_stall_s);
        } else
        fprintf(stderr, "child exit=%d wall=%.4fs (no --aux-out; switch records only)\n", WIFEXITED(status)?WEXITSTATUS(status):-1, wall);
        if (ncpu) for (int e = 0; e < nev; e++)
            fprintf(stderr, "  cpu %d: aux_bytes=%llu lost=%llu truncated=%llu switch_records=%llu itrace_start=%llu data_lost=%llu%s\n",
                    dctx[e].cpu, (unsigned long long)dctx[e].aux_written, (unsigned long long)dctx[e].aux_lost,
                    (unsigned long long)dctx[e].aux_trunc_recs, (unsigned long long)dctx[e].n_sw,
                    (unsigned long long)dctx[e].n_itrace, (unsigned long long)dctx[e].data_lost_recs,
                    dctx[e].data_lost_recs ? "  [switch records LOST: enlarge --data-pages]" : "");
        if (g_aux_fulllap || g_stale)
            fprintf(stderr, "error: ToPA overflow signature: %llu AUX records larger than half the ring, %llu stale ring bytes -- the kernel mis-placed "
                            "the ToPA write offset (mixed-order AUX chunks, chunk order %d); the capture is NOT trustworthy. Lower --aux-mb or --watermark-kb.\n",
                    (unsigned long long)g_aux_fulllap, (unsigned long long)g_stale, g_topa_order);
        fprintf(stderr, "ToPA: full-lap AUX records=%llu stale bytes=%llu chunk order %d\n", (unsigned long long)g_aux_fulllap, (unsigned long long)g_stale, g_topa_order);
        if (g_aux_lost || g_aux_trunc_recs)
            fprintf(stderr, "warning: the AUX reader fell behind the producer -- %llu bytes LOST "
                            "(%llu truncation records, %llu re-enables); the trace has GAPS. "
                            "Enlarge --aux-mb, write to a faster disk, or give pt_capture2 a core "
                            "of its own (the drain thread inherits the parent's affinity)\n",
                    (unsigned long long)g_aux_lost, (unsigned long long)g_aux_trunc_recs,
                    (unsigned long long)g_aux_resumed);
        if (g_data_lost_recs)
            fprintf(stderr, "warning: %llu PERF_RECORD_LOST in the data ring\n", (unsigned long long)g_data_lost_recs);
        if (draining == 2) {   /* no AUX file: the ring holds the (prefix of the) trace */
            uint64_t aux_head = __atomic_load_n(&pc->aux_head, __ATOMIC_ACQUIRE);
            wrapped = (aux_head >= aux_len); nbytes = wrapped ? aux_len : (size_t)aux_head;
        }
    } else {
        uint64_t aux_head = __atomic_load_n(&pc->aux_head, __ATOMIC_ACQUIRE);
        /* aux_head saturating at aux_len means the software AUX buffer filled and
         * the trace was TRUNCATED (distinct from hardware on-chip OVF) -- enlarge
         * --aux-mb, drop --no-drain, or use a smaller dataset. */
        wrapped = (aux_head >= aux_len);
        nbytes = wrapped ? aux_len : (size_t)aux_head;
        fprintf(stderr, "child exit=%d wall=%.4fs aux_bytes=%zu%s (aux_buf=%ldMB)\n",
                WIFEXITED(status)?WEXITSTATUS(status):-1, wall, nbytes,
                wrapped ? " [WRAPPED-software loss, enlarge --aux-mb or drop --no-drain]" : "", aux_mb);
        if (aux_out) {
            FILE *af = fopen(aux_out, "wb");
            if (!af) perror("aux-out"); else { if (fwrite(aux, 1, nbytes, af) != nbytes) perror("aux-out write"); fclose(af); }
            if (wrapped) fprintf(stderr, "warning: AUX wrapped; the saved trace is TRUNCATED (head lost)\n");
        }
        for (int e = 0; e < nev; e++) { couts[e].cpu = dctx[e].cpu; couts[e].aux = aux_names[e]; couts[e].sw = NULL; }
    }
    if (g_maps_dropped) fprintf(stderr, "error: %llu mappings were DROPPED from the sideband\n", (unsigned long long)g_maps_dropped);
    if (sideband) write_sideband(sideband, child, status, nbytes, wrapped, mtc_period, pc, couts, ncpu ? nev : 0);
    if (no_decode) return WIFEXITED(status)?WEXITSTATUS(status):-1;  /* skip scan (overhead runs) */
    /* The ring was consumed while the child ran, so the packet scan reads the
     * SAVED FILE -- which is the whole trace, not just the last ring-full.  With
     * several per-CPU files only the FIRST is scanned here (the scan is a
     * diagnostic; ptrecon reads them all). */
    if (draining == 1) {
        int rfd = open(aux_names[0], O_RDONLY);
        if (rfd < 0) { perror("aux-out reopen"); return 2; }
        nbytes = (size_t)dctx[0].aux_written;
        if (nbytes) {
            scan_map_len = nbytes;
            scan_base = mmap(NULL, scan_map_len, PROT_READ, MAP_PRIVATE, rfd, 0);
            if (scan_base == MAP_FAILED) { perror("mmap aux-out"); close(rfd); return 2; }
        } else scan_base = aux;
        close(rfd);
    }

    /* 4. Packet-level scan with the image-free packet decoder. */
    struct pt_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.size = sizeof(cfg);
    cfg.begin = (uint8_t *)scan_base;
    cfg.end = (uint8_t *)scan_base + nbytes;
    struct pt_packet_decoder *dec = pt_pkt_alloc_decoder(&cfg);
    if (!dec) { fprintf(stderr, "pt_pkt_alloc_decoder failed\n"); return 2; }

    uint64_t cnt[ppt_mwait+8]; memset(cnt, 0, sizeof(cnt));
    uint64_t n_ovf=0, n_tsc=0, n_mtc=0, n_cyc=0, n_psb=0, n_ptw=0, n_total=0;
    /* OVF timeline: for each OVF, record (tsc_before, gap) where tsc_before is
     * the last TSC before the gap and gap = (TSC when PT resumes) - tsc_before
     * = the duration of control flow LOST to that overflow (in TSC cycles).
     * Pending OVFs are flushed on the next TSC packet. */
    FILE *tlf = timeline ? fopen(timeline, "w") : NULL;
    uint64_t last_tsc = 0, first_tsc = 0, total_lost = 0;
    uint64_t *pend = NULL; size_t np = 0, pcap = 0;
    /* --ptw-window: bin PTW packets into fixed TSC windows (real-time rate distribution).
     * Fixed TSC rate constant (matches the analysis scripts). */
    const double TSC_PER_US = 3878.205567;
    uint64_t win_tsc = (uint64_t)(ptwwin_us * TSC_PER_US);
    FILE *pdf = ptwdump ? fopen(ptwdump, "w") : NULL;   /* --ptw-dump: one PTW payload per line */
    if (win_tsc == 0) win_tsc = 1;
    uint64_t *wins = NULL; size_t wcap = 0, wmax = 0;
    int err = pt_pkt_sync_forward(dec);
    while (err >= 0) {
        struct pt_packet pkt;
        int e = pt_pkt_next(dec, &pkt, sizeof(pkt));
        if (e < 0) {
            /* resync to the next PSB on decode error */
            err = pt_pkt_sync_forward(dec);
            continue;
        }
        n_total++;
        switch (pkt.type) {
            case ppt_ovf: n_ovf++;
                if (tlf) { if (np == pcap) { pcap = pcap ? pcap*2 : 1024; pend = realloc(pend, pcap*sizeof(*pend)); }
                           pend[np++] = last_tsc; }
                break;
            case ppt_tsc: n_tsc++;
                { uint64_t t = pkt.payload.tsc.tsc;
                  if (tlf) for (size_t j = 0; j < np; j++) {
                      uint64_t g = t > pend[j] ? t - pend[j] : 0; total_lost += g;
                      fprintf(tlf, "%llu %llu\n", (unsigned long long)pend[j], (unsigned long long)g);
                  }
                  np = 0; last_tsc = t; if (!first_tsc) first_tsc = t; }
                break;
            case ppt_mtc: n_mtc++; break;
            case ppt_cyc: n_cyc++; break;
            case ppt_psb: n_psb++; break;
            case ppt_ptw: n_ptw++;
                if (pdf) fprintf(pdf, "%llu\n", (unsigned long long)pkt.payload.ptw.payload);
                if (ptwwin && first_tsc && last_tsc >= first_tsc) {
                    size_t w = (size_t)((last_tsc - first_tsc) / win_tsc);
                    if (w >= wcap) {
                        size_t nc = wcap ? wcap*2 : 4096; while (w >= nc) nc *= 2;
                        wins = realloc(wins, nc*sizeof(*wins));
                        for (size_t z = wcap; z < nc; z++) wins[z] = 0;
                        wcap = nc;
                    }
                    wins[w]++; if (w+1 > wmax) wmax = w+1;
                }
                break;
            default: break;
        }
    }
    pt_pkt_free_decoder(dec);
    if (pdf) fclose(pdf);
    if (tlf) {
        for (size_t j = 0; j < np; j++) {            /* OVFs with no resume TSC yet */
            uint64_t g = last_tsc > pend[j] ? last_tsc - pend[j] : 0; total_lost += g;
            fprintf(tlf, "%llu %llu\n", (unsigned long long)pend[j], (unsigned long long)g);
        }
        fprintf(tlf, "# first_tsc %llu last_tsc %llu n_ovf %llu total_lost_cycles %llu\n",
                (unsigned long long)first_tsc, (unsigned long long)last_tsc,
                (unsigned long long)n_ovf, (unsigned long long)total_lost);
        fclose(tlf); free(pend);
    }

    if (ptwwin) {
        FILE *pwf = fopen(ptwwin, "w");
        if (pwf) {
            fprintf(pwf, "# window_us %ld win_tsc %llu n_windows %zu (count = PTW packets per window)\n",
                    ptwwin_us, (unsigned long long)win_tsc, wmax);
            for (size_t w = 0; w < wmax; w++)
                fprintf(pwf, "%zu %llu\n", w, (unsigned long long)(wins ? wins[w] : 0));
            fclose(pwf);
        }
        free(wins);
    }
    printf("OVF=%lu TSC=%lu MTC=%lu CYC=%lu PSB=%lu PTW=%lu total_packets=%lu\n",
           n_ovf, n_tsc, n_mtc, n_cyc, n_psb, n_ptw, n_total);
    printf("aux_bytes=%zu wall=%.4fs aux_rate=%.1f MB/s mtc_rate=%.2f MHz\n",
           nbytes, wall, nbytes/1e6/wall, n_mtc/1e6/wall);
    if (scan_map_len) munmap(scan_base, scan_map_len);
    return 0;
}
