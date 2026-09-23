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
// The tool updates its map, re-translates the affected range
// (PIN_RemoveInstrumentationInRange) and stores 1 into `acknowledged`.  A front end that
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

struct JitObject { ADDRINT length; std::map<ADDRINT, Site> sites; };
static std::map<ADDRINT, JitObject> g_jitobjects;
static PIN_LOCK g_jitlock;
static UINT64 g_jitadded = 0, g_jitmoved = 0, g_jitremoved = 0, g_jitrequested = 0;

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

static VOID JitPublish(ADDRINT address, THREADID tid)
{
    PtPinRequest req;
    if (PIN_SafeCopy(&req, reinterpret_cast<const void *>(address), sizeof req) != sizeof req ||
        req.version != 1 || req.acknowledged || !jit_range(req.base, req.length))
        jit_fail("bad request/range");
    JitObject object; object.length = req.length;
    if (req.operation == PT_PIN_ADD) {
        if (req.count > 65536) jit_fail("too many sites");
        std::vector<PtjSite> sites(req.count);
        const size_t bytes = sites.size() * sizeof(PtjSite);
        if (bytes && PIN_SafeCopy(&sites[0], reinterpret_cast<const void *>(req.sites), bytes) != bytes)
            jit_fail("unreadable sites");
        std::stable_sort(sites.begin(), sites.end(), [](const PtjSite &a, const PtjSite &b) {
            return a.id < b.id;
        });
        for (const auto &s : sites) {
            if (s.off >= req.length || s.when > PTJ_WHEN_AFTER || s.kind > PTJ_KIND_MEMOP ||
                s.kf || s.nregs > PTJ_MAXREG) jit_fail("unsupported site/keyframe");
            Site &target = object.sites[s.off];
            auto &items = (s.when == PTJ_WHEN_AFTER || s.kind == PTJ_KIND_LOAD) ? target.after : target.before;
            if (s.kind == PTJ_KIND_MEMOP) {
                if (s.when != PTJ_WHEN_BEFORE || s.size < 1 || s.size > 8) jit_fail("bad memory site");
                items.push_back(Item{IT_MEM, REG_INVALID(), s.size});
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
    std::vector<std::pair<ADDRINT, ADDRINT> > invalidated;
    invalidated.push_back(std::make_pair(req.base, req.length));
    PIN_GetLock(&g_jitlock, tid + 1);
    if (req.operation == PT_PIN_ADD) {
        jit_erase_overlaps(req.base, req.length, &invalidated);
        g_jitobjects[req.base] = object;
        jit_widen(req.base, req.length);
        g_jitadded++; g_jitrequested += req.count;
    } else if (req.operation == PT_PIN_MOVE) {
        auto old = g_jitobjects.find(req.base);
        if (old != g_jitobjects.end() && old->second.length == req.length) {
            object = old->second;
            g_jitobjects.erase(old);
            jit_erase_overlaps(req.destination, req.length, &invalidated);
            g_jitobjects[req.destination] = object;
            jit_widen(req.destination, req.length);
            g_jitmoved++;
        }
        // Unanalyzed/gated code can also move: it has no sites to republish.
    } else {
        jit_erase_overlaps(req.base, req.length, &invalidated); g_jitremoved++;
    }
    PIN_ReleaseLock(&g_jitlock);
    for (const auto &range : invalidated)
        PIN_RemoveInstrumentationInRange(range.first, range.first + range.second - 1);
    if (req.operation == PT_PIN_MOVE)
        PIN_RemoveInstrumentationInRange(req.destination, req.destination + req.length - 1);
    const UINT64 ack = 1;
    if (PIN_SafeCopy(reinterpret_cast<void *>(address + offsetof(PtPinRequest, acknowledged)),
                     &ack, sizeof ack) != sizeof ack) jit_fail("unwritable acknowledgement");
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
        if (s != it->second.sites.end()) { *out = s->second; found = true; }
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
