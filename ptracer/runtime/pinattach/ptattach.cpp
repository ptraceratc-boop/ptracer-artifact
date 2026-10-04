/*
 * ptattach.cpp -- attach PTracer's Fast instrumentation to a running process (and detach it).
 *
 * A Pin 3.20 Pintool in PROBE mode that is ONLY the attach/detach vehicle.
 * It inserts no Pin probes and no Pin analysis routines at all: the
 * instrumentation is PTracer's own in-place trampolines, i.e. exactly the bytes
 * `runtime/rewrite.py' + E9Patch write into a statically rewritten image.
 *
 *   attach:  pin -pid PID -t ptattach.so -plan PLAN.pplan -detach-after 4
 *            -> for every loaded image the plan knows:
 *                 (1) mmap the E9Patch trampoline regions at base+va
 *                     (MAP_FIXED_NOREPLACE; a conflict is reported, never
 *                     silently relocated -- a relocated trampoline is out of
 *                     jmp rel32 range and would be wrong, not slow),
 *                 (2) verify the ORIGINAL bytes at every patch site and write
 *                     the detour bytes in place,
 *                 (3) resume; the application runs natively with PTracer's
 *                     trampolines, at the static rewrite's speed.
 *   detach:  restore the original bytes, munmap the trampolines,
 *            PIN_DetachProbed().  The application keeps running, uninstrumented.
 *
 * The plan comes from `patchplan.py' (which imports rewrite.py as a library and
 * never modifies it).
 *
 * Threading: the patch and the restore both mutate live code.  Pin holds the
 * application at attach (all IMG callbacks run before probing starts), so the
 * apply is safe.  The restore runs from PIN_AddThreadDetachProbedFunction,
 * which Pin delivers to every application thread just before it removes probes
 * -- i.e. with every thread parked inside Pin -- which is the most quiescent
 * point probe mode offers.  `-restore now' restores from the internal thread
 * instead (racy on a multithreaded target; used to measure the restore cost).
 */

#include "pin.H"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <map>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* ------------------------------------------------------------------ knobs */

KNOB<std::string> KnobPlan(KNOB_MODE_WRITEONCE, "pintool", "plan", "",
    "binary patch plan from patchplan.py (PLAN.pplan)");
KNOB<std::string> KnobLog(KNOB_MODE_WRITEONCE, "pintool", "o", "",
    "log file (default: stderr)");
KNOB<double> KnobDetachAfter(KNOB_MODE_WRITEONCE, "pintool", "detach-after", "0",
    "detach this many seconds after the application resumes (0 = never)");
KNOB<std::string> KnobTrigger(KNOB_MODE_WRITEONCE, "pintool", "trigger", "",
    "detach as soon as this file exists (polled every -poll-ms)");
KNOB<UINT32> KnobPollMs(KNOB_MODE_WRITEONCE, "pintool", "poll-ms", "5",
    "trigger poll interval, milliseconds");
KNOB<BOOL> KnobApply(KNOB_MODE_WRITEONCE, "pintool", "apply", "1",
    "0 = attach and detach without applying the plan (measures the bare "
    "Pin probe-mode attach/detach cost)");
KNOB<std::string> KnobRestore(KNOB_MODE_WRITEONCE, "pintool", "restore", "thread",
    "when to restore the original bytes: 'thread' (in the per-thread "
    "detach-probed callback, the quiescent point), 'now' (from the internal "
    "thread, before requesting detach), 'never' (leave the process patched)");
KNOB<std::string> KnobReady(KNOB_MODE_WRITEONCE, "pintool", "ready", "",
    "create this file once the plan has been applied (attach handshake)");
KNOB<std::string> KnobDone(KNOB_MODE_WRITEONCE, "pintool", "done", "",
    "create this file once the detach has completed");
KNOB<BOOL> KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "v", "0", "verbose");
KNOB<BOOL> KnobUnmapOnDetach(KNOB_MODE_WRITEONCE, "pintool", "unmap-on-detach", "0",
    "munmap the trampoline regions at detach.  UNSAFE and off by default: a "
    "thread can be executing INSIDE a trampoline at the moment Pin parks it for "
    "detach, and unmapping the page under it kills the process.  Restoring the "
    "original bytes is enough to make the process uninstrumented; the regions "
    "are kept mapped (and REUSED by the next attach cycle) so any thread still "
    "inside a trampoline can finish and jump back into the restored code.");
KNOB<BOOL> KnobSkipConflicts(KNOB_MODE_WRITEONCE, "pintool", "skip-conflicts", "0",
    "when a trampoline region's address is already taken, install the rest of "
    "the image anyway and skip only the CLUSTERS of sites that depend on the "
    "missing region (patchplan.py records, per cluster, the SET of regions its "
    "detours jump into).  0 (the DEFAULT) = install an image only if EVERY "
    "region of its plan mapped, otherwise refuse the image and leave it "
    "untouched.  A PARTIALLY applied E9Patch image is not a "
    "less-instrumented program, it is a CRASH -- E9Patch's T2/T3 tactics evict "
    "instructions into the trampoline, so a detour whose cluster was skipped "
    "can still be reached through a neighbouring punned jump.  Set to 1 only to study the "
    "collisions, never to trace.");
KNOB<UINT32> KnobCycles(KNOB_MODE_WRITEONCE, "pintool", "cycles", "1",
    "repeat attach/detach this many times.  A SECOND `pin -pid' on the same "
    "process fails with \"Pin is already attached\" -- after PIN_DetachProbed() "
    "the Pin runtime stays resident and only PIN_AttachProbed() from the same "
    "tool can bring it back, which is what this does.");
KNOB<UINT32> KnobGapMs(KNOB_MODE_WRITEONCE, "pintool", "gap-ms", "300",
    "detached interval between cycles, milliseconds");

/* -------------------------------------------------------------- the plan */

/* Must match patchplan.py's HDR_FMT / IMG_FMT / PATCH_FMT / MAP_FMT. */
#pragma pack(push, 1)
struct PlanHdr {
    char magic[8];
    uint32_t version, n_images;
    uint64_t img_off, str_off, blob_off, blob_len;
};
struct PlanImg {
    uint64_t key_off, path_off, orig_size, link_low, link_end;
    uint32_t pie, n_patches;
    uint64_t patch_off;
    uint32_t n_maps, _pad;
    uint64_t map_off;
    uint8_t sha[32];
    uint32_t n_clusters;
    uint64_t cluster_off, regidx_off;
};
struct PlanCluster { uint32_t first, count; };
struct PlanPatch {
    uint64_t va, old_off, new_off;
    uint32_t len, region, cluster;
};
struct PlanMap {
    uint64_t va, len, blob_off;
    uint32_t prot, type;
};
#pragma pack(pop)

static const char *MAPTYPE[4] = { "trampoline", "reserve", "refactor-tail",
                                  "loader" };

static uint8_t *g_plan = NULL;          /* mmap of the whole plan file */
static size_t g_plan_len = 0;
static PlanHdr *g_hdr = NULL;
static const char *g_str = NULL;
static const uint8_t *g_blob = NULL;

/* what we actually did, so detach can undo it exactly */
struct AppliedMap { void *addr; size_t len; };
struct AppliedImage {
    const PlanImg *img;
    std::string name;
    ADDRINT base;
    std::vector<AppliedMap> maps;
    std::vector<uint8_t> region_ok;   /* per plan region: did it map? */
    std::vector<uint8_t> cluster_bad; /* per cluster: some patch is uninstallable */
    bool any_conflict;
    bool patched;
};
static std::vector<AppliedImage> g_applied;
/* Regions this tool has mapped and still owns, across detach/attach cycles:
 * they are deliberately NOT unmapped at detach (see -unmap-on-detach). */
static std::map<ADDRINT, size_t> g_owned;
static PIN_LOCK g_lock;

/* ------------------------------------------------------------------- log */

static int g_logfd = 2;

static void LOG_(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        ssize_t w = write(g_logfd, buf, n);
        (void)w;
    }
}

/* Pin's private C runtime exports gettimeofday() but NOT clock_gettime(); a
 * tool that references clock_gettime fails to dlopen ("cannot locate symbol").
 * gettimeofday's resolution (1 us) is far finer than anything measured here. */
static double now_sec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

static void touch(const std::string &p)
{
    if (p.empty()) return;
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);
}

/* ------------------------------------------------------- plan file access */

static bool plan_load(const std::string &path)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) { LOG_("[ptattach] cannot open plan %s: %s\n", path.c_str(),
                       strerror(errno)); return false; }
    struct stat st;
    if (fstat(fd, &st)) { close(fd); return false; }
    g_plan_len = (size_t)st.st_size;
    void *m = mmap(NULL, g_plan_len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) { LOG_("[ptattach] mmap plan failed\n"); return false; }
    g_plan = (uint8_t *)m;
    g_hdr = (PlanHdr *)g_plan;
    if (memcmp(g_hdr->magic, "PTPLAN01", 8) != 0 || g_hdr->version != 1) {
        LOG_("[ptattach] %s is not a v1 PTPLAN file\n", path.c_str());
        return false;
    }
    g_str = (const char *)(g_plan + g_hdr->str_off);
    g_blob = g_plan + g_hdr->blob_off;
    return true;
}

static const PlanImg *plan_image(unsigned i)
{
    return (const PlanImg *)(g_plan + g_hdr->img_off + i * sizeof(PlanImg));
}

/* ---------------------------------------------------------- page helpers */

static const size_t PAGE = 4096;

/* Who owns `addr' right now?  A trampoline address E9Patch chose for a file it
 * OWNED can be occupied in a live process; naming the occupant is the whole
 * diagnosis. */
static void occupant(ADDRINT addr, char *out, size_t n)
{
    out[0] = 0;
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return;
    std::string all;
    char buf[65536];
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0) all.append(buf, (size_t)r);
    close(fd);
    size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find('\n', p);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(p, e - p);
        p = e + 1;
        unsigned long lo = 0, hi = 0;
        if (sscanf(line.c_str(), "%lx-%lx", &lo, &hi) != 2) continue;
        if (addr >= lo && addr < hi) {
            snprintf(out, n, "%s", line.c_str());
            return;
        }
    }
    snprintf(out, n, "(nothing mapped there -- mmap refused for another reason)");
}

/* ------------------------------------------------------------ apply plan */

/* Is this patch installable?  It is not if the region its detour jumps into
 * could not be mapped -- or, when the region could not be identified at plan
 * time (region == ~0u), if ANY region of the image is missing. */
/* A patch is applied only if EVERY trampoline region its CLUSTER jumps into
 * was mapped -- see cluster_patches()/cluster_region_sets() in patchplan.py.
 * A cluster with an empty region set ("could not tell") is never applied once
 * the image has any collision. */
static inline bool patch_enabled(const AppliedImage &ai, const PlanPatch &p)
{
    if (!ai.any_conflict) return true;
    return p.cluster < ai.cluster_bad.size() && !ai.cluster_bad[p.cluster];
}

/* Sites the plan cannot install here, with the reason.  Reported at detach. */
static unsigned g_conflicts = 0, g_mismatches = 0;
/* Self-check: bytes read back after the write / after the restore. */
static unsigned g_write_bad = 0, g_restore_bad = 0, g_skipped = 0;

static bool apply_image(const PlanImg *pi, IMG img)
{
    AppliedImage ai;
    ai.img = pi;
    ai.name = IMG_Name(img);
    ai.base = IMG_LowAddress(img) - pi->link_low;
    ai.patched = false;
    ai.any_conflict = false;
    ai.region_ok.assign(pi->n_maps, 0);

    const PlanPatch *P = (const PlanPatch *)(g_plan + pi->patch_off);
    const PlanMap *M = (const PlanMap *)(g_plan + pi->map_off);

    /* (0) IDENTITY.  The plan was derived from a rewritten COPY of this image;
     * if the live image is a different build, the recorded original bytes will
     * not match.  Check them ALL before writing anything -- a half-applied
     * plan is a crash. */
    for (unsigned i = 0; i < pi->n_patches; i++) {
        const uint8_t *want = g_blob + P[i].old_off;
        if (memcmp((const void *)(ai.base + P[i].va), want, P[i].len) != 0) {
            LOG_("[ptattach] %s: ORIGINAL BYTES MISMATCH at +%#lx (%u B) -- the "
                 "live image is not the one the plan was built from; image "
                 "SKIPPED\n", ai.name.c_str(), (unsigned long)P[i].va,
                 P[i].len);
            g_mismatches++;
            return false;
        }
    }

    /* (1) the regions the rewritten image ADDS: E9Patch trampolines (and the
     * reserve region of a --delta build).  They must land at base+va: E9Patch
     * encoded the jump into them as a rel32 (often a PUNNED one whose bytes
     * come from the original instruction), so the address is not negotiable. */
    for (unsigned i = 0; i < pi->n_maps; i++) {
        void *want = (void *)(ai.base + M[i].va);
        int prot = ((M[i].prot & 1) ? PROT_READ : 0) |
                   ((M[i].prot & 2) ? PROT_WRITE : 0) |
                   ((M[i].prot & 4) ? PROT_EXEC : 0);
        std::map<ADDRINT, size_t>::iterator own = g_owned.find((ADDRINT)want);
        if (own != g_owned.end() && own->second == M[i].len) {
            /* a previous cycle's region, still ours: refresh it in place */
            if (!mprotect(want, M[i].len, PROT_READ | PROT_WRITE)) {
                memcpy(want, g_blob + M[i].blob_off, M[i].len);
                mprotect(want, M[i].len, prot);
                ai.region_ok[i] = 1;
                AppliedMap am; am.addr = want; am.len = M[i].len;
                ai.maps.push_back(am);
                continue;
            }
        }
        void *got = mmap(want, M[i].len, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                         -1, 0);
        if (got == MAP_FAILED || got != want) {
            char who[512];
            occupant((ADDRINT)want, who, sizeof who);
            LOG_("[ptattach] %s: CONFLICT at %p (%lu B, %s): %s\n"
                 "               occupied by: %s\n",
                 ai.name.c_str(), want, (unsigned long)M[i].len,
                 MAPTYPE[M[i].type & 3],
                 got == MAP_FAILED ? strerror(errno) : "address taken", who);
            if (got != MAP_FAILED && got != want) munmap(got, M[i].len);
            g_conflicts++;
            ai.any_conflict = true;
            if (!KnobSkipConflicts.Value()) {
                for (size_t k = 0; k < ai.maps.size(); k++)
                    munmap(ai.maps[k].addr, ai.maps[k].len);
                return false;
            }
            continue;             /* the sites that jump here are skipped below */
        }
        memcpy(got, g_blob + M[i].blob_off, M[i].len);
        if (prot != (PROT_READ | PROT_WRITE) && mprotect(got, M[i].len, prot))
            LOG_("[ptattach] warning: mprotect(%p) failed: %s\n", got,
                 strerror(errno));
        ai.region_ok[i] = 1;
        g_owned[(ADDRINT)got] = M[i].len;
        AppliedMap am; am.addr = got; am.len = M[i].len;
        ai.maps.push_back(am);
    }

    /* (1b) a collision disables whole CLUSTERS of patches, and a cluster
     * depends on the SET of regions its detours jump into. */
    if (ai.any_conflict) {
        const PlanCluster *C = (const PlanCluster *)(g_plan + pi->cluster_off);
        const uint32_t *R = (const uint32_t *)(g_plan + pi->regidx_off);
        ai.cluster_bad.assign(pi->n_clusters, 0);
        for (unsigned c = 0; c < pi->n_clusters; c++) {
            if (C[c].count == 0) { ai.cluster_bad[c] = 1; continue; }
            for (unsigned k = 0; k < C[c].count; k++) {
                uint32_t r = R[C[c].first + k];
                if (r >= ai.region_ok.size() || !ai.region_ok[r]) {
                    ai.cluster_bad[c] = 1;
                    break;
                }
            }
        }
    }

    /* (2) the in-place detours.  Grouped by page so we mprotect once per page
     * run instead of once per site (CPython has ~28k of them). */
    unsigned i = 0;
    while (i < pi->n_patches) {
        unsigned j = i;
        ADDRINT lo = ai.base + P[i].va;
        ADDRINT hi = lo + P[i].len;
        while (j + 1 < pi->n_patches &&
               ((ai.base + P[j + 1].va) & ~(ADDRINT)(PAGE - 1)) <=
                   ((hi + PAGE - 1) & ~(ADDRINT)(PAGE - 1))) {
            j++;
            hi = ai.base + P[j].va + P[j].len;
        }
        ADDRINT p0 = lo & ~(ADDRINT)(PAGE - 1);
        ADDRINT p1 = (hi + PAGE - 1) & ~(ADDRINT)(PAGE - 1);
        if (mprotect((void *)p0, p1 - p0,
                     PROT_READ | PROT_WRITE | PROT_EXEC)) {
            LOG_("[ptattach] %s: mprotect(%p,%lu,rwx) failed: %s\n",
                 ai.name.c_str(), (void *)p0, (unsigned long)(p1 - p0),
                 strerror(errno));
            for (size_t k = 0; k < ai.maps.size(); k++)
                munmap(ai.maps[k].addr, ai.maps[k].len);
            return false;
        }
        for (unsigned k = i; k <= j; k++) {
            if (!patch_enabled(ai, P[k])) { g_skipped++; continue; }
            memcpy((void *)(ai.base + P[k].va), g_blob + P[k].new_off,
                   P[k].len);
            if (memcmp((const void *)(ai.base + P[k].va),
                       g_blob + P[k].new_off, P[k].len) != 0)
                g_write_bad++;
        }
        if (mprotect((void *)p0, p1 - p0, PROT_READ | PROT_EXEC))
            LOG_("[ptattach] warning: mprotect back failed: %s\n",
                 strerror(errno));
        i = j + 1;
    }
    ai.patched = true;
    g_applied.push_back(ai);
    return true;
}

static void restore_image(AppliedImage &ai)
{
    const PlanImg *pi = ai.img;
    const PlanPatch *P = (const PlanPatch *)(g_plan + pi->patch_off);
    if (ai.patched) {
        unsigned i = 0;
        while (i < pi->n_patches) {
            unsigned j = i;
            ADDRINT lo = ai.base + P[i].va;
            ADDRINT hi = lo + P[i].len;
            while (j + 1 < pi->n_patches &&
                   ((ai.base + P[j + 1].va) & ~(ADDRINT)(PAGE - 1)) <=
                       ((hi + PAGE - 1) & ~(ADDRINT)(PAGE - 1))) {
                j++;
                hi = ai.base + P[j].va + P[j].len;
            }
            ADDRINT p0 = lo & ~(ADDRINT)(PAGE - 1);
            ADDRINT p1 = (hi + PAGE - 1) & ~(ADDRINT)(PAGE - 1);
            if (!mprotect((void *)p0, p1 - p0,
                          PROT_READ | PROT_WRITE | PROT_EXEC)) {
                for (unsigned k = i; k <= j; k++) {
                    if (!patch_enabled(ai, P[k])) continue;
                    memcpy((void *)(ai.base + P[k].va), g_blob + P[k].old_off,
                           P[k].len);
                    if (memcmp((const void *)(ai.base + P[k].va),
                               g_blob + P[k].old_off, P[k].len) != 0)
                        g_restore_bad++;
                }
                mprotect((void *)p0, p1 - p0, PROT_READ | PROT_EXEC);
            }
            i = j + 1;
        }
        ai.patched = false;
    }
    /* The trampolines are unmapped only AFTER the detours are gone -- and by
     * default not at all: Pin can park a thread while its rip is INSIDE a
     * trampoline, and unmapping the page under it is fatal. */
    if (KnobUnmapOnDetach.Value()) {
        for (size_t k = 0; k < ai.maps.size(); k++) {
            munmap(ai.maps[k].addr, ai.maps[k].len);
            g_owned.erase((ADDRINT)ai.maps[k].addr);
        }
    }
    ai.maps.clear();
}

/* ------------------------------------------------------------- callbacks */

static double g_t_attach0 = 0, g_t_attach1 = 0;
static double g_t_detach_req = 0, g_t_restore0 = 0, g_t_restore1 = 0,
              g_t_detached = 0;
static volatile bool g_restored = false;
static volatile bool g_detach_requested = false;
static unsigned g_n_images_applied = 0, g_n_patches_applied = 0,
                g_n_maps_applied = 0;

static VOID ImageLoad(IMG img, VOID *);
static VOID AppStart(VOID *);
static double g_apply_ms = 0;

static VOID ImageLoad(IMG img, VOID *)
{
    if (!KnobApply.Value() || g_plan == NULL) return;
    if (g_detach_requested) return;             /* detaching: do not patch more */

    const char *nm = IMG_Name(img).c_str();
    const char *slash = strrchr(nm, '/');
    std::string basen = slash ? slash + 1 : nm;

    for (unsigned i = 0; i < g_hdr->n_images; i++) {
        const PlanImg *pi = plan_image(i);
        const char *key = g_str + pi->key_off;
        const char *path = g_str + pi->path_off;
        if (basen != key && IMG_Name(img) != path) continue;
        double t0 = now_sec();
        PIN_GetLock(&g_lock, 1);
        bool ok = apply_image(pi, img);
        if (ok) {
            g_n_images_applied++;
            g_n_patches_applied += pi->n_patches;
            g_n_maps_applied += pi->n_maps;
        }
        PIN_ReleaseLock(&g_lock);
        double t1 = now_sec();
        g_apply_ms += (t1 - t0) * 1e3;
        LOG_("[ptattach] %-24s base=%p %s  patches=%u maps=%u  %.3f ms\n",
             basen.c_str(), (void *)(IMG_LowAddress(img) - pi->link_low),
             ok ? "APPLIED" : "FAILED", pi->n_patches, pi->n_maps,
             (t1 - t0) * 1e3);
        return;
    }
    if (KnobVerbose.Value())
        LOG_("[ptattach] (no plan for %s)\n", basen.c_str());
}

static void do_restore(const char *who)
{
    PIN_GetLock(&g_lock, 1);
    if (!g_restored) {
        g_t_restore0 = now_sec();
        for (size_t i = 0; i < g_applied.size(); i++) restore_image(g_applied[i]);
        g_t_restore1 = now_sec();
        g_restored = true;
        LOG_("[ptattach] restored %lu image(s) from %s in %.3f ms\n",
             (unsigned long)g_applied.size(), who,
             (g_t_restore1 - g_t_restore0) * 1e3);
    }
    PIN_ReleaseLock(&g_lock);
}

static volatile bool g_detach_done = false, g_app_started = false;
static unsigned g_cycle = 1;

static VOID ThreadDetachProbed(VOID *)
{
    if (KnobRestore.Value() == "thread") do_restore("thread-detach callback");
}

static VOID DetachCompletedProbed(VOID *)
{
    g_t_detached = now_sec();
    if (KnobRestore.Value() != "never") do_restore("detach-completed callback");
    LOG_("[ptattach] CYCLE %u images=%u patches=%u maps=%u conflicts=%u "
         "mismatches=%u skipped=%u write_bad=%u restore_bad=%u\n", g_cycle,
         g_n_images_applied, g_n_patches_applied, g_n_maps_applied,
         g_conflicts, g_mismatches, g_skipped, g_write_bad, g_restore_bad);
    LOG_("[ptattach] TIMING cycle=%u attach_ms=%.3f apply_ms=%.3f "
         "detach_total_ms=%.3f restore_ms=%.3f\n", g_cycle,
         (g_t_attach1 - g_t_attach0) * 1e3, g_apply_ms,
         (g_t_detached - g_t_detach_req) * 1e3,
         (g_t_restore1 - g_t_restore0) * 1e3);
    touch(KnobDone.Value());
    g_detach_done = true;
}

/* Re-attach entry point (PIN_AttachProbed): Pin calls this the moment it has
 * the application back, and the tool has to re-register everything it wants
 * for the new session -- see AttachDetach/reattach_probed_tool.cpp. */
static VOID AttachMain(VOID *)
{
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddApplicationStartFunction(AppStart, 0);
    PIN_AddThreadDetachProbedFunction(ThreadDetachProbed, 0);
    PIN_AddDetachFunctionProbed(DetachCompletedProbed, 0);
}

static VOID DetachThread(VOID *)
{
    const std::string trig = KnobTrigger.Value();
    for (unsigned c = 1; ; c++) {
        double deadline = KnobDetachAfter.Value() > 0
                              ? g_t_attach1 + KnobDetachAfter.Value() : 0;
        for (;;) {
            if (deadline > 0 && now_sec() >= deadline) break;
            if (!trig.empty()) {
                struct stat st;
                if (stat(trig.c_str(), &st) == 0) break;
            }
            if (deadline == 0 && trig.empty()) { PIN_Sleep(1000); continue; }
            PIN_Sleep(KnobPollMs.Value());
        }
        g_detach_requested = true;
        g_t_detach_req = now_sec();
        if (KnobRestore.Value() == "now") do_restore("internal thread");
        LOG_("[ptattach] requesting PIN_DetachProbed() (cycle %u)\n", c);
        PIN_DetachProbed();
        if (c >= KnobCycles.Value()) break;
        while (!g_detach_done) PIN_Sleep(1);
        PIN_Sleep(KnobGapMs.Value());
        /* start the next session from a clean slate */
        PIN_GetLock(&g_lock, 1);
        g_applied.clear();
        g_restored = false; g_detach_requested = false; g_detach_done = false;
        g_app_started = false;
        g_n_images_applied = g_n_patches_applied = g_n_maps_applied = 0;
        g_conflicts = g_mismatches = g_skipped = 0;
        g_write_bad = g_restore_bad = 0;
        g_apply_ms = 0;   /* per-cycle, not cumulative */
        g_cycle = c + 1;
        PIN_ReleaseLock(&g_lock);
        g_t_attach0 = now_sec();
        if (PIN_AttachProbed(AttachMain, 0) != ATTACH_INITIATED) {
            LOG_("[ptattach] PIN_AttachProbed refused at cycle %u\n", c + 1);
            break;
        }
        while (!g_app_started) PIN_Sleep(1);
    }
    LOG_("[ptattach] cycles finished\n");
}

static VOID AppStart(VOID *)
{
    static bool spawned = false;
    g_t_attach1 = now_sec();
    LOG_("[ptattach] attach %u complete: %u image(s), %u patches, %u maps, "
         "%.3f ms\n", g_cycle, g_n_images_applied, g_n_patches_applied,
         g_n_maps_applied, (g_t_attach1 - g_t_attach0) * 1e3);
    touch(KnobReady.Value());
    g_app_started = true;
    if (!spawned &&
        (KnobDetachAfter.Value() > 0 || !KnobTrigger.Value().empty())) {
        spawned = true;
        PIN_THREAD_UID uid;
        if (PIN_SpawnInternalThread(DetachThread, 0, 0, &uid) == INVALID_THREADID)
            LOG_("[ptattach] PIN_SpawnInternalThread failed -- no auto detach\n");
    }
}

static INT32 Usage()
{
    fprintf(stderr,
            "ptattach: apply PTracer's E9Patch trampolines to a LIVE process.\n"
            "  pin -pid PID -t ptattach.so -plan PLAN.pplan "
            "[-detach-after SEC] [-trigger FILE]\n%s\n",
            KNOB_BASE::StringKnobSummary().c_str());
    return -1;
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitLock(&g_lock);
    if (!KnobLog.Value().empty()) {
        int fd = open(KnobLog.Value().c_str(),
                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) g_logfd = fd;
    }
    g_t_attach0 = now_sec();
    if (KnobApply.Value()) {
        if (KnobPlan.Value().empty()) {
            LOG_("[ptattach] -plan is required (or -apply 0)\n");
            return 1;
        }
        if (!plan_load(KnobPlan.Value())) return 1;
        LOG_("[ptattach] plan %s: %u image(s), blob %lu B\n",
             KnobPlan.Value().c_str(), g_hdr->n_images,
             (unsigned long)g_hdr->blob_len);
    }
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddApplicationStartFunction(AppStart, 0);
    PIN_AddThreadDetachProbedFunction(ThreadDetachProbed, 0);
    PIN_AddDetachFunctionProbed(DetachCompletedProbed, 0);
    PIN_StartProgramProbed();
    return 0;
}
