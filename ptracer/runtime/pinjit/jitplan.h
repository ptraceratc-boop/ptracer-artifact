// jitplan.h -- runtime-published plans for generated code (the `-jitbridge` knob).
// Included by hifitool.cpp after its Site/Item definitions; published sites go through
// exactly the same emit_items / trace-buffer path as ELF PLAN sites.
//
// Protocol.  A JIT front end (runtime/jit/jithook.cc for V8, runtime/jit/java/jvmtiagent.cc
// for HotSpot) exports the inert marker function `ptj_pin_publish(PtPinRequest *)` of
// jitbridge.h.  When an image defining it loads, the tool instruments that function; every
// call carries one request (all fields 64-bit, see jitbridge.h):
//   PT_PIN_ADD      a code object [base, base+length) with `count` PtjSite records at
//                   `sites` (offsets relative to base, jitsite_format.h).  Replaces every
//                   plan overlapping the range.
//   PT_PIN_MOVE     the object at `base` was relocated to `destination` (same length).
//   PT_PIN_REMOVE   the object at `base` is gone.
// The tool records the request and stores 1 into `acknowledged`; the plan change and the
// re-translation of the affected ranges (PIN_RemoveInstrumentationInRange) are applied in
// batches by the tool's own thread (see "Batched hand-over" below).  A front end that
// sees no acknowledgement is running without this tool and must refuse to continue rather
// than run uninstrumented.
#include "jitbridge.h"
#include <algorithm>
#include <cstddef>
#include <limits>

// On a target that generates no code and loads no publisher this costs one symbol lookup
// per image at load time and, per translated instruction, two compares (see g_jitlo/hi).
KNOB<BOOL> KnobJitBridge(KNOB_MODE_WRITEONCE, "pintool", "jitbridge", "1",
                         "accept runtime-published JIT critical-site plans");

/* Batched hand-over.  Re-translating each published range from the publishing thread, one
 * PIN_RemoveInstrumentationInRange per request, can drive Pin into a storm of unlinked VM transfers
 * (under Pin's VM lock) once the hot generated code is published; applying the same requests in
 * batches avoids it.  So
 * JitPublish only queues the request, and an internal Pin thread applies the queue and re-translates
 * the merged ranges once no request came for -jitquietms, or at the latest -jitbatchms after the
 * oldest queued one.  A published object is therefore instrumented up to that long after its
 * publication -- the same kind of window as the asynchronous analysis that precedes every
 * publication.  -jitbatch 0 restores the synchronous per-request path. */
KNOB<BOOL> KnobJitBatch(KNOB_MODE_WRITEONCE, "pintool", "jitbatch", "1",
                        "apply JIT publications in batches from a tool thread (0 = synchronously, per request)");
KNOB<UINT32> KnobJitBatchMs(KNOB_MODE_WRITEONCE, "pintool", "jitbatchms", "10000",
                            "latest hand-over of a queued JIT publication, ms after the oldest queued one");
KNOB<BOOL> KnobJitBatchStop(KNOB_MODE_WRITEONCE, "pintool", "jitbatchstop", "1",
                             "stop the application threads around a JIT hand-over batch");
KNOB<UINT32> KnobJitQuietMs(KNOB_MODE_WRITEONCE, "pintool", "jitquietms", "2000",
                            "hand over the queued JIT publications once none came for this many ms");

/* A plan and its code key: (offset, first two code bytes at the ADD) of every planned site, by offset. */
struct JitObject { ADDRINT length; std::map<ADDRINT, Site> sites; std::vector<std::pair<UINT32, UINT16> > key; };
static std::map<ADDRINT, JitObject> g_jitobjects;
static PIN_LOCK g_jitlock;
static UINT64 g_jitadded = 0, g_jitmoved = 0, g_jitremoved = 0, g_jitrequested = 0;
static UINT64 g_jitkeymiss = 0;

/* Live-byte check.  With batching, the table can lag the code: a queued REMOVE of an object whose memory the
 * JVM has already reused for new code leaves the OLD plan in the table until the batch, and a translation made
 * in between would instrument the new code with the old plan's sites.  So a plan is applied to an instruction
 * only while the LIVE code bytes still match the plan's key (the first two bytes of every planned site,
 * recorded at the ADD; the site itself plus an even sample of at most 32 entries on its page is compared at
 * each instrumentation).  On a mismatch the instruction gets no JIT site (unlogged, never wrong), counted as
 * jit_keymiss; the batch that applies the queued requests re-translates the range with the right plan.
 *
 * The key: plain volatile reads, no PIN_SafeCopy (see jit_copy_in).  Two bytes are prefix/REX/opcode/ModRM; the
 * fields the JVM patches in live code (call displacements, oop/klass/guard immediates) lie beyond them. */
static inline UINT16 jit_code2(ADDRINT at, ADDRINT left)
{
    volatile const UINT8 *p = reinterpret_cast<volatile const UINT8 *>(at);
    return (UINT16)(p[0] | (left > 1 ? (UINT16)p[1] << 8 : 0));
}

static void jit_key_record(ADDRINT base, JitObject &object)
{
    object.key.clear();
    object.key.reserve(object.sites.size());
    for (const auto &s : object.sites)
        object.key.push_back(std::make_pair((UINT32)s.first, jit_code2(base + s.first, object.length - s.first)));
}

/* Compares the entry for `off' (if planned; it must match) and an even sample of at most 32 entries ON THE SAME
 * PAGE as `off' -- the page being translated, so certainly mapped -- and never reads the second byte across a
 * page boundary.  Not cached across translations on purpose: the case it exists for is exactly a change of the
 * bytes under an unchanged table. */
static inline bool jit_key_entry_ok(ADDRINT at, UINT16 want)
{
    volatile const UINT8 *p = reinterpret_cast<volatile const UINT8 *>(at);
    if (p[0] != (UINT8)want) return false;
    return ((at + 1) & 0xfff) == 0 || p[1] == (UINT8)(want >> 8);
}

static bool jit_key_match(ADDRINT base, const JitObject &object, ADDRINT off)
{
    typedef std::pair<UINT32, UINT16> K;
    const std::vector<K> &k = object.key;
    if (k.empty()) return true;
    const ADDRINT page = (base + off) & ~(ADDRINT)0xfff;
    const ADDRINT lo = page > base ? page - base : 0, hi = page + 0x1000 - base;
    auto own = std::lower_bound(k.begin(), k.end(), K((UINT32)off, 0));
    if (own != k.end() && own->first == off && !jit_key_entry_ok(base + own->first, own->second)) return false;
    auto b = std::lower_bound(k.begin(), k.end(), K((UINT32)lo, 0));
    auto e = std::lower_bound(b, k.end(), K((UINT32)std::min<ADDRINT>(hi, object.length), 0));
    /* One mismatching sample among at least four is tolerated: HotSpot (JDK 17) makes an nmethod not entrant
     * by writing a 5-byte jmp over its verified entry, which can cover one planned site of a still-running
     * nmethod.  Different code at a reused address matches few of the sampled opcode pairs. */
    const size_t n = e - b, step = n > 32 ? n / 32 : 1;
    size_t checked = 0, bad = 0;
    for (size_t i = 0; i < n; i += step, checked++)
        if (!jit_key_entry_ok(base + b[i].first, b[i].second)) bad++;
    return bad == 0 || (bad == 1 && checked >= 4);
}

/* The published objects' bounding range, WIDENED ONLY, read without the lock.  On a
 * target that publishes nothing the range is empty and the per-instruction hook is two
 * compares and no lock.  A torn or stale read can only make the check conservative or
 * make one translation miss a just-published object -- and that publication invalidates
 * the range right after, forcing the translation to be redone. */
static volatile ADDRINT g_jitlo = ~(ADDRINT)0, g_jithi = 0;

static void jit_widen(ADDRINT base, ADDRINT length)
{
    if (base < g_jitlo) g_jitlo = base;
    if (base + length > g_jithi) g_jithi = base + length;
}

static void jit_fail(const char *why)
{
    fprintf(stderr, "[hifitool] JIT bridge refused: %s\n", why);
    PIN_ExitProcess(71);
}

static bool jit_range(ADDRINT base, ADDRINT size)
{
    return base && size && size <= (1u << 27) &&
           base <= std::numeric_limits<ADDRINT>::max() - size;
}

// Caller holds only our data lock.  Never call Pin invalidation while holding it: an
// instrumentation callback may be waiting on us while owning Pin's VM lock.
static void jit_erase_overlaps(ADDRINT base, ADDRINT length,
                              std::vector<std::pair<ADDRINT, ADDRINT> > *invalidated)
{
    for (auto it = g_jitobjects.begin(); it != g_jitobjects.end();) {
        if (it->first < base + length && base < it->first + it->second.length) {
            invalidated->push_back(std::make_pair(it->first, it->second.length));
            std::map<ADDRINT, JitObject>::iterator dead = it++;
            g_jitobjects.erase(dead);
        } else ++it;
    }
}

/* JIT-bridge request reads WITHOUT PIN_SafeCopy (the same reason as hifitool's m: loads): on some
 * CPUs a PIN_SafeCopy call from an
 * analysis routine leaves dirty upper AVX state, after which every SSE instruction in the code cache pays
 * ASSISTS.SSE_AVX_MIX.  JitPublish runs once per JIT publication (thousands per Node.js or Java run).  The request and
 * its site array live in the tracee's own heap, written by the JIT hook on the publishing thread just before
 * the marker call, so a plain word copy (no AVX in the tool's own code) is safe; a bad request pointer
 * would fault in the tool instead of reaching jit_fail. */
static inline void jit_copy_in(void *dst, ADDRINT src, size_t n)
{
    volatile const UINT8 *s = reinterpret_cast<volatile const UINT8 *>(src);
    UINT8 *d = static_cast<UINT8 *>(dst);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { UINT64 w = *reinterpret_cast<volatile const UINT64 *>(s + i); std::memcpy(d + i, &w, 8); }
    for (; i < n; i++) d[i] = s[i];
}

// Caller holds g_jitlock.  `invalidated' receives every range to re-translate.
static void jit_apply(const PtPinRequest &req, JitObject &object,
                      std::vector<std::pair<ADDRINT, ADDRINT> > *invalidated)
{
    invalidated->push_back(std::make_pair(req.base, req.length));
    if (req.operation == PT_PIN_MOVE)
        invalidated->push_back(std::make_pair(req.destination, req.length));
    if (req.operation == PT_PIN_ADD) {
        jit_erase_overlaps(req.base, req.length, invalidated);
        g_jitobjects[req.base] = object;
        jit_widen(req.base, req.length);
        g_jitadded++; g_jitrequested += req.count;
    } else if (req.operation == PT_PIN_MOVE) {
        auto old = g_jitobjects.find(req.base);
        if (old != g_jitobjects.end() && old->second.length == req.length) {
            object = old->second;
            g_jitobjects.erase(old);
            jit_erase_overlaps(req.destination, req.length, invalidated);
            g_jitobjects[req.destination] = object;
            jit_widen(req.destination, req.length);
            g_jitmoved++;
        }
        // Unanalyzed/gated code can also move: it has no sites to republish.
    } else {
        jit_erase_overlaps(req.base, req.length, invalidated); g_jitremoved++;
    }
}

static std::vector<std::pair<PtPinRequest, JitObject> > g_jitqueue;   // guarded by g_jitlock
static double g_jitqueue_first = 0, g_jitqueue_last = 0;
static UINT64 g_jitbatches = 0, g_jitranges = 0, g_jitstopped = 0;
static volatile bool g_jitstop = false;

// The batching thread (a Pin internal thread; runs until PrepareForFini).
static VOID JitBatchThread(VOID *)
{
    while (!g_jitstop) {
        struct timespec ts = {0, 20000000L};                  // 20 ms
        nanosleep(&ts, NULL);
        PIN_GetLock(&g_jitlock, 1);
        const double now = now_s();
        const bool due = !g_jitqueue.empty() && (now - g_jitqueue_last >= KnobJitQuietMs.Value() / 1000.0 ||
                                                 now - g_jitqueue_first >= KnobJitBatchMs.Value() / 1000.0);
        PIN_ReleaseLock(&g_jitlock);
        if (!due) continue;
        /* The plan change and its re-translation happen with every application thread stopped between
         * traces (PIN_StopApplicationThreads from this internal thread) and under Pin's client lock: no
         * thread is inside a trace translated with the old plan when the new one becomes visible, so Pin's
         * re-instrumentation of a trace that takes an exception never sees a different plan
         * ("inconsistent instrumentation during exception handling" abort otherwise). */
        const bool stopped = KnobJitBatchStop.Value() && PIN_StopApplicationThreads(PIN_ThreadId());
        PIN_LockClient();
        PIN_GetLock(&g_jitlock, 1);
        std::vector<std::pair<ADDRINT, ADDRINT> > ranges;
        for (auto &q : g_jitqueue) jit_apply(q.first, q.second, &ranges);
        g_jitqueue.clear();
        PIN_ReleaseLock(&g_jitlock);
        std::sort(ranges.begin(), ranges.end());
        ADDRINT lo = 0, hi = 0;
        bool have = false;
        for (const auto &r : ranges) {
            if (have && r.first <= hi) { hi = std::max(hi, r.first + r.second); continue; }
            if (have) { PIN_RemoveInstrumentationInRange(lo, hi - 1); g_jitranges++; }
            lo = r.first; hi = r.first + r.second; have = true;
        }
        if (have) { PIN_RemoveInstrumentationInRange(lo, hi - 1); g_jitranges++; }
        PIN_UnlockClient();
        if (stopped) PIN_ResumeApplicationThreads(PIN_ThreadId());
        g_jitbatches++; if (stopped) g_jitstopped++;
    }
}
static VOID JitBatchStop(VOID *) { g_jitstop = true; }

static VOID JitPublish(ADDRINT address, THREADID tid)
{
    PtPinRequest req;
    jit_copy_in(&req, address, sizeof req);
    if (req.version != 1 || req.acknowledged || !jit_range(req.base, req.length))
        jit_fail("bad request/range");
    JitObject object; object.length = req.length;
    if (req.operation == PT_PIN_ADD) {
        if (req.count > 65536) jit_fail("too many sites");
        std::vector<PtjSite> sites(req.count);
        const size_t bytes = sites.size() * sizeof(PtjSite);
        if (bytes && !req.sites) jit_fail("unreadable sites");
        if (bytes) jit_copy_in(&sites[0], (ADDRINT)req.sites, bytes);
        std::stable_sort(sites.begin(), sites.end(), [](const PtjSite &a, const PtjSite &b) {
            return a.id < b.id;
        });
        for (const auto &s : sites) {
            if (s.off >= req.length || s.when > PTJ_WHEN_AFTER || s.kind > PTJ_KIND_MEMOP ||
                s.kf || s.nregs > PTJ_MAXREG) jit_fail("unsupported site/keyframe");
            Site &target = object.sites[s.off];
            auto &items = (s.when == PTJ_WHEN_AFTER || s.kind == PTJ_KIND_LOAD) ? target.after : target.before;
            if (s.kind == PTJ_KIND_MEMOP) {
                if (s.when != PTJ_WHEN_BEFORE || s.size < 1 || (s.size > 8 && (s.size % 8 || s.size > 64)))
                    jit_fail("bad memory site");
                if (s.size <= 8) items.push_back(Item{IT_MEM, REG_INVALID(), s.size, 0});
                else   /* a 16/32/64-byte vector operand: logged as its 8-byte slices, low first */
                    for (UINT32 o = 0; o < s.size; o += 8) items.push_back(Item{IT_MEM, REG_INVALID(), 8, o});
            } else {
                if (!s.nregs) jit_fail("empty register site");
                for (unsigned k = 0; k < s.nregs; k++) {
                    const unsigned r = s.regs[k];
                    if (r >= 32) jit_fail("unsupported register");
                    static const char *gp[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                               "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
                    if (r < 16) items.push_back(Item{IT_REG, reg_by_name(gp[r]), 8});
                    else {
                        items.push_back(Item{IT_XMM_LO, (REG)(REG_XMM0 + r - 16), 8});
                        items.push_back(Item{IT_XMM_HI, (REG)(REG_XMM0 + r - 16), 8});
                    }
                }
            }
        }
    } else if (req.operation != PT_PIN_MOVE && req.operation != PT_PIN_REMOVE) {
        jit_fail("unknown operation");
    }
    if (req.operation == PT_PIN_MOVE && !jit_range(req.destination, req.length))
        jit_fail("bad destination");
    if (req.operation == PT_PIN_ADD) jit_key_record(req.base, object);   /* the code is installed at the ADD */
    std::vector<std::pair<ADDRINT, ADDRINT> > invalidated;
    if (KnobJitBatch.Value()) {
        PIN_GetLock(&g_jitlock, tid + 1);
        const double now = now_s();
        if (g_jitqueue.empty()) g_jitqueue_first = now;
        g_jitqueue_last = now;
        g_jitqueue.push_back(std::make_pair(req, object));
        PIN_ReleaseLock(&g_jitlock);
    } else {
        PIN_GetLock(&g_jitlock, tid + 1);
        jit_apply(req, object, &invalidated);
        PIN_ReleaseLock(&g_jitlock);
        for (const auto &range : invalidated)
            PIN_RemoveInstrumentationInRange(range.first, range.first + range.second - 1);
    }
    const UINT64 ack = 1;
    *reinterpret_cast<volatile UINT64 *>(address + offsetof(PtPinRequest, acknowledged)) = ack;
}

static bool jit_site(ADDRINT address, Site *out)
{
    if (address < g_jitlo || address >= g_jithi) return false;   /* lock-free common case */
    bool found = false;
    PIN_GetLock(&g_jitlock, 1);
    auto it = g_jitobjects.upper_bound(address);
    if (it != g_jitobjects.begin()) {
        --it;
        auto s = it->second.sites.find(address - it->first);
        if (s != it->second.sites.end()) {
            if (jit_key_match(it->first, it->second, address - it->first)) { *out = s->second; found = true; }
            else g_jitkeymiss++;
        }
    }
    PIN_ReleaseLock(&g_jitlock);
    return found;
}

static void jit_image(IMG image)
{
    if (!KnobJitBridge.Value()) return;
    RTN marker = RTN_FindByName(image, "ptj_pin_publish");
    if (!RTN_Valid(marker)) return;
    RTN_Open(marker);
    RTN_InsertCall(marker, IPOINT_BEFORE, (AFUNPTR)JitPublish,
                   IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_THREAD_ID, IARG_END);
    RTN_Close(marker);
    fprintf(stderr, "[hifitool] JIT marker in %s\n", IMG_Name(image).c_str());
}
