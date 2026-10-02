/*
 * hifitool.cpp -- the HiFi Pintool: Intel Pin JIT-mode instrumentation that logs the
 * critical values a spec names, at the addresses it names, in program order.
 *
 * Site sources.  (1) An ELF PLAN (`-plan`, produced by mkplan.py from spec JSON files;
 * the grammar is documented there), resolved against IMG_LoadOffset() when the image
 * loads.  (2) Plans for generated code, published at run time by a JIT front end
 * (runtime/jit/jithook.cc for V8, runtime/jit/java/jvmtiagent.cc for HotSpot) through
 * the marker function of jitbridge.h (`-jitbridge`, on by default; see jitplan.h).
 * Both sources feed the same emitter.
 *
 * Logging.  Pin's buffering API: PIN_DefineTraceBuffer with a record size of EIGHT
 * BYTES and one INS_InsertFillBuffer per logged value.  A fill buffer is filled by
 * inlined code in the code cache, not by a callout, so the common case costs one store
 * and a pointer bump.  Because a record IS one little-endian 64-bit value, the buffer's
 * raw bytes are already the body of the critical-value file:
 *
 *     cv.<pid>.<tid>.bin  =  struct { char magic[4]="PTCV"; uint32 version=2;
 *                                     uint32 tid; uint32 sync; }
 *                            followed by raw little-endian uint64 values in program order.
 *
 * `sync` is 0: this tool emits no sync markers.
 *
 * Knobs:
 *   -plan  FILE       the PLAN from mkplan.py (optional when -jitbridge supplies sites)
 *   -cvdir DIR        where cv.<pid>.<tid>.bin go; "/dev/null" discards (timing runs)
 *   -pages N          trace-buffer size in 4 KB pages per thread (default 8192 = 32 MB)
 *   -stats 1          print per-run counters to stderr at exit
 *   -sink buffer|ptwrite|count
 *                     buffer (default): the inlined fill buffer above.
 *                     ptwrite: every logged value becomes an analysis routine that executes
 *                     one `ptwrite %rdi` (Pin 4.4 inlines it; the cost is the PTWRITE itself);
 *                     the values travel in the Intel PT packet stream and no cv file is
 *                     written.  Outside a PT capture (IA32_RTIT_CTL.PTWEn clear) the
 *                     instruction retires without a packet, so the run is still correct.
 *   -ptwbuffer FILE   with -sink ptwrite: the MIXED sink.  The ELF-plan sites FILE lists, and every
 *                     runtime-published (JIT) site, log through the buffer; every other ELF-plan site
 *                     through PTWRITE.  FILE = the sites over the PTWRITE budget (hifi_ptw_budget.py,
 *                     from a -sink count profile): lines `<image basename> <hex link-time address>'.
 *   -sink count       the buffer sink plus one inlined counter per ELF-plan address; at exit each
 *                     process writes <-countout>.<pid>.tsv (wall seconds, then image, address,
 *                     values per execution, executions) for hifi_ptw_budget.py.
 *   -jitbridge 0|1    accept runtime-published plans for generated code (default 1)
 *   -memread load|safecopy
 *                     how an m: item reads memory.  load (default): an inlined exact-width
 *                     load where it provably cannot fault unless the instruction itself is
 *                     about to (a fault there is logged as 0, as PIN_SafeCopy did, and the
 *                     application then takes its own fault); PIN_SafeCopy callout elsewhere.
 *                     safecopy: the callout everywhere.
 */
#include "pin.H"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <set>
#include <sys/time.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

/* ------------------------------------------------------------------ knobs */
KNOB<std::string> KnobPlan(KNOB_MODE_WRITEONCE, "pintool", "plan", "",
                           "PLAN file from runtime/pinjit/mkplan.py");
KNOB<std::string> KnobCvDir(KNOB_MODE_WRITEONCE, "pintool", "cvdir", ".",
                            "directory for cv.<pid>.<tid>.bin ('/dev/null' discards)");
KNOB<UINT32> KnobPages(KNOB_MODE_WRITEONCE, "pintool", "pages", "8192",
                       "trace-buffer pages per thread (4 KB each)");
KNOB<BOOL> KnobStats(KNOB_MODE_WRITEONCE, "pintool", "stats", "0", "print stats at exit");
KNOB<std::string> KnobSink(KNOB_MODE_WRITEONCE, "pintool", "sink", "buffer",
                           "value sink: buffer (inlined fill buffer) | ptwrite (ptwrite per value) | count (buffer + site profile)");
KNOB<std::string> KnobPtwBuffer(KNOB_MODE_WRITEONCE, "pintool", "ptwbuffer", "",
                                "with -sink ptwrite: sites kept on the buffer (mixed sink)");
KNOB<std::string> KnobCountOut(KNOB_MODE_WRITEONCE, "pintool", "countout", "hifi_count",
                               "-sink count: output prefix (<prefix>.<pid>.tsv)");
/* How an m:<bytes> item reads the instruction's memory operand.
 * "load" (default): an INLINED plain load of exactly <bytes> at IARG_MEMORYREAD_EA, used only when the
 * instruction itself unconditionally reads at least <bytes> there (standard memop, no REP, not
 * predicated), so the load can fault only where the application instruction would fault next.
 * Every other m: site keeps the PIN_SafeCopy callout.  "safecopy": a PIN_SafeCopy callout at every
 * m: site.  On some CPUs a PIN_SafeCopy callout from instrumented code can leave dirty upper AVX
 * state, so the rest of the run pays ASSISTS.SSE_AVX_MIX on every SSE instruction; the logged value
 * is identical. */
KNOB<std::string> KnobMemRead(KNOB_MODE_WRITEONCE, "pintool", "memread", "load",
                              "m: item read: load (inlined, safe cases) | safecopy (PIN_SafeCopy callout)");

/* ------------------------------------------------------------------- plan */
enum ItemKind { IT_REG, IT_XMM_LO, IT_XMM_HI, IT_MEM, IT_FSBASE };

struct Item {
    ItemKind kind;
    REG reg;        /* IT_REG / IT_XMM_* */
    UINT32 size;    /* IT_MEM */
    UINT32 off;     /* IT_MEM: byte offset of this 8-byte slice in a wide (> 8 byte) operand */
};

struct Site {
    std::vector<Item> before;
    std::vector<Item> after;
};

/* plan as read: image path -> (link-time address -> Site) */
static std::map<std::string, std::map<ADDRINT, Site> > g_plan;
/* resolved at image load: runtime address -> Site* */
static std::map<ADDRINT, const Site *> g_sites;
static UINT64 g_resolved = 0, g_images = 0;
/* mixed sink: runtime addresses of the ELF-plan sites kept on the buffer */
static std::set<std::pair<std::string, ADDRINT> > g_bufsites;   /* (image basename, link address) */
static std::set<ADDRINT> g_bufaddr;
static bool g_mixed = false, g_count = false;
static UINT64 g_ptw_sites = 0, g_buf_sites = 0;
/* -sink count: one counter per resolved ELF-plan address */
struct CountRec { std::string img; ADDRINT link; UINT32 nval; UINT64 n; };
static std::map<ADDRINT, CountRec *> g_counts;
static double g_t0 = 0;
static double now_s() { struct timeval tv; gettimeofday(&tv, NULL); return tv.tv_sec + tv.tv_usec * 1e-6; }
static std::string base_of(const std::string &n)
{
    size_t sl = n.rfind('/');
    return sl == std::string::npos ? n : n.substr(sl + 1);
}

static REG reg_by_name(const std::string &n)
{
    static const char *gp[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                 "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    /* Pin 4's POSIX headers also define global REG_R* ucontext enumerators. */
    static const REG gr[16] = {LEVEL_BASE::REG_RAX, LEVEL_BASE::REG_RCX,
        LEVEL_BASE::REG_RDX, LEVEL_BASE::REG_RBX, LEVEL_BASE::REG_RSP,
        LEVEL_BASE::REG_RBP, LEVEL_BASE::REG_RSI, LEVEL_BASE::REG_RDI,
        LEVEL_BASE::REG_R8, LEVEL_BASE::REG_R9, LEVEL_BASE::REG_R10,
        LEVEL_BASE::REG_R11, LEVEL_BASE::REG_R12, LEVEL_BASE::REG_R13,
        LEVEL_BASE::REG_R14, LEVEL_BASE::REG_R15};
    for (int i = 0; i < 16; i++)
        if (n == gp[i])
            return gr[i];
    if (n.size() > 3 && n.compare(0, 3, "xmm") == 0) {
        int k = atoi(n.c_str() + 3);
        if (k >= 0 && k < 16)
            return (REG)(REG_XMM0 + k);
    }
    return REG_INVALID();
}

static bool parse_item(const std::string &s, Item *out)
{
    if (s == "f") { out->kind = IT_FSBASE; out->reg = REG_INVALID(); out->size = 8; return true; }
    if (s.compare(0, 2, "r:") == 0) {
        out->kind = IT_REG; out->size = 8; out->reg = reg_by_name(s.substr(2));
        return out->reg != REG_INVALID();
    }
    if (s.compare(0, 2, "m:") == 0) {
        out->kind = IT_MEM; out->reg = REG_INVALID(); out->off = 0;
        out->size = (UINT32)atoi(s.c_str() + 2);
        return out->size >= 1 && out->size <= 8;
    }
    if (s.compare(0, 2, "x:") == 0) {
        size_t c = s.rfind(':');
        if (c == std::string::npos || c < 2) return false;
        out->reg = reg_by_name(s.substr(2, c - 2));
        out->kind = (s.compare(c + 1, std::string::npos, "hi") == 0) ? IT_XMM_HI : IT_XMM_LO;
        out->size = 8;
        return out->reg != REG_INVALID();
    }
    return false;
}

static void load_plan(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "[hifitool] cannot open plan %s\n", path.c_str());
        PIN_ExitProcess(2);
    }
    char line[65536];
    std::string img;
    UINT64 nsite = 0, nval = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'I' && line[1] == ' ') {
            char *p = line + 2, *e = p + strlen(p);
            while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) --e;
            img.assign(p, e - p);
        } else if (line[0] == 'S' && line[1] == ' ') {
            char *save = NULL;
            strtok_r(line, " \t\n", &save);                 /* "S" */
            char *a = strtok_r(NULL, " \t\n", &save);
            char *w = strtok_r(NULL, " \t\n", &save);
            char *n = strtok_r(NULL, " \t\n", &save);
            if (!a || !w || !n) continue;
            ADDRINT addr = (ADDRINT)strtoull(a, NULL, 16);
            int cnt = atoi(n);
            Site &st = g_plan[img][addr];
            std::vector<Item> &v = (w[0] == 'A') ? st.after : st.before;
            for (int i = 0; i < cnt; i++) {
                char *t = strtok_r(NULL, " \t\n", &save);
                Item it;
                if (!t || !parse_item(std::string(t), &it)) {
                    fprintf(stderr, "[hifitool] bad plan item near %s\n", a);
                    PIN_ExitProcess(2);
                }
                v.push_back(it);
                nval++;
            }
            nsite++;
        }
    }
    fclose(f);
    fprintf(stderr, "[hifitool] plan: %lu images, %lu addresses, %lu values/execution\n",
            (unsigned long)g_plan.size(), (unsigned long)nsite, (unsigned long)nval);
}

/* --------------------------------------------------------- per-thread sink */
struct CvHdr { char magic[4]; UINT32 version; UINT32 tid; UINT32 sync; };

struct TLS {
    int fd;
    UINT64 nvalues;
    OS_THREAD_ID ostid;
};
static TLS_KEY g_tls;
static std::vector<TLS *> g_all;
static PIN_LOCK g_lock;
static BUFFER_ID g_buf;
static bool g_discard = false;

static VOID ThreadStart(THREADID tid, CONTEXT *, INT32, VOID *)
{
    TLS *t = new TLS;
    t->nvalues = 0;
    t->ostid = PIN_GetTid();
    char path[1024];
    if (g_discard)
        snprintf(path, sizeof(path), "/dev/null");
    else
        snprintf(path, sizeof(path), "%s/cv.%d.%d.bin", KnobCvDir.Value().c_str(),
                 (int)PIN_GetPid(), (int)t->ostid);
    t->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (t->fd >= 0 && !g_discard) {
        CvHdr h;
        memcpy(h.magic, "PTCV", 4);
        h.version = 2;
        h.tid = (UINT32)t->ostid;
        h.sync = 0;                     /* no sync markers, see the header comment */
        if (write(t->fd, &h, sizeof(h)) != (ssize_t)sizeof(h))
            fprintf(stderr, "[hifitool] short header write\n");
    }
    PIN_SetThreadData(g_tls, t, tid);
    PIN_GetLock(&g_lock, tid + 1);
    g_all.push_back(t);
    PIN_ReleaseLock(&g_lock);
}

static VOID ThreadFini(THREADID tid, const CONTEXT *, INT32, VOID *)
{
    TLS *t = (TLS *)PIN_GetThreadData(g_tls, tid);
    if (t && t->fd >= 0) {
        close(t->fd);
        t->fd = -1;
    }
}

/* Pin calls this when a thread's trace buffer is full and once when it exits. */
static VOID *BufferFull(BUFFER_ID, THREADID tid, const CONTEXT *, VOID *buf,
                        UINT64 n, VOID *)
{
    TLS *t = (TLS *)PIN_GetThreadData(g_tls, tid);
    if (t) {
        t->nvalues += n;
        if (t->fd >= 0 && n) {
            size_t want = (size_t)n * 8, off = 0;
            const char *p = (const char *)buf;
            while (off < want) {
                ssize_t w = write(t->fd, p + off, want - off);
                if (w <= 0) break;
                off += (size_t)w;
            }
        }
    }
    return buf;
}

/* ------------------------------------------------- analysis for the odd cases */
/* A memop logs the VALUE at the instruction's effective address; a fill buffer cannot
 * dereference, so the value is produced into a tool scratch register first and the fill
 * buffer copies that register.  Both are inserted at IPOINT_BEFORE, and Pin runs analysis
 * inserted at the same point in insertion order, so program order is preserved. */
static REG g_scr;

static ADDRINT PIN_FAST_ANALYSIS_CALL ReadMem(ADDRINT ea, UINT32 sz)
{
    UINT64 v = 0;
    PIN_SafeCopy(&v, (const VOID *)ea, sz);
    return (ADDRINT)v;
}
static ADDRINT PIN_FAST_ANALYSIS_CALL ReadMemOff(ADDRINT ea, ADDRINT off) { return ReadMem(ea + off, 8); }

/* Inlinable exact-width loads (straight-line, no call): Pin inlines them into the code cache, so
 * no bridge and no PIN_SafeCopy runs.  Zero-extension matches ReadMem's zero-initialised copy of
 * the low <sz> little-endian bytes. */
static ADDRINT PIN_FAST_ANALYSIS_CALL Load8(ADDRINT ea) { return *(const volatile UINT64 *)ea; }
static ADDRINT PIN_FAST_ANALYSIS_CALL Load4(ADDRINT ea) { return *(const volatile UINT32 *)ea; }
static ADDRINT PIN_FAST_ANALYSIS_CALL Load2(ADDRINT ea) { return *(const volatile UINT16 *)ea; }
static ADDRINT PIN_FAST_ANALYSIS_CALL Load1(ADDRINT ea) { return *(const volatile UINT8 *)ea; }
/* one 8-byte slice of a wide (16/32/64-byte) vector memory operand */
static ADDRINT PIN_FAST_ANALYSIS_CALL Load8Off(ADDRINT ea, ADDRINT off) { return *(const volatile UINT64 *)(ea + off); }
static ADDRINT PIN_FAST_ANALYSIS_CALL ReadMemOff(ADDRINT ea, ADDRINT off);
static bool g_memload = true;
static UINT64 g_mem_inline = 0, g_mem_safecopy = 0, g_mem_faults = 0;

/* A fault in inlined analysis code is a Pin INTERNAL exception, which by default kills the process
 * ("Tool (or Pin) caused signal 11") instead of reaching the application's own handler.  The inlined
 * load faults only where the instrumented instruction is about to fault on the same bytes (a JVM
 * implicit null check, a probing SIGSEGV handler, ...).  This handler gives such a fault exactly the
 * PIN_SafeCopy semantics: the logged value is 0, execution resumes after the load, and the
 * application instruction then raises the real, application-visible fault.  It only accepts a
 * faulting MOV/MOVZX whose destination is a GPR and whose source is memory, i.e. the Load* body;
 * any other internal fault is left to Pin (EHR_CONTINUE_SEARCH). */
extern "C" {
#include "xed-interface.h"
}
static EXCEPT_HANDLING_RESULT LoadFault(THREADID, EXCEPTION_INFO *ei, PHYSICAL_CONTEXT *pc, VOID *)
{
    if (PIN_GetExceptionClass(PIN_GetExceptionCode(ei)) != EXCEPTCLASS_ACCESS_FAULT)
        return EHR_CONTINUE_SEARCH;
    const ADDRINT ip = PIN_GetPhysicalContextReg(pc, REG_INST_PTR);
    UINT8 bytes[15];
    size_t got = PIN_SafeCopy(bytes, (const VOID *)ip, sizeof bytes);
    if (got == 0)
        return EHR_CONTINUE_SEARCH;
    xed_decoded_inst_t x;
    xed_decoded_inst_zero(&x);
    xed_decoded_inst_set_mode(&x, XED_MACHINE_MODE_LONG_64, XED_ADDRESS_WIDTH_64b);
    if (xed_decode(&x, bytes, (unsigned)got) != XED_ERROR_NONE)
        return EHR_CONTINUE_SEARCH;
    const xed_iclass_enum_t ic = xed_decoded_inst_get_iclass(&x);
    if ((ic != XED_ICLASS_MOV && ic != XED_ICLASS_MOVZX) ||
        xed_decoded_inst_number_of_memory_operands(&x) != 1 ||
        !xed_decoded_inst_mem_read(&x, 0) || xed_decoded_inst_mem_written(&x, 0))
        return EHR_CONTINUE_SEARCH;
    const REG dst = INS_XedExactMapToPinReg(xed_decoded_inst_get_reg(&x, XED_OPERAND_REG0));
    if (!REG_is_gr(REG_FullRegName(dst)))
        return EHR_CONTINUE_SEARCH;
    PIN_SetPhysicalContextReg(pc, REG_FullRegName(dst), 0);
    PIN_SetPhysicalContextReg(pc, REG_INST_PTR, ip + xed_decoded_inst_get_length(&x));
    __atomic_add_fetch(&g_mem_faults, 1, __ATOMIC_RELAXED);
    return EHR_HANDLED;
}

/* The inlined load is used only where it cannot fault unless the instrumented instruction
 * itself is about to fault on the same bytes. */
static AFUNPTR inline_loader(INS ins, UINT32 sz)
{
    if (!g_memload || !INS_IsMemoryRead(ins) || !INS_IsStandardMemop(ins) ||
        INS_HasRealRep(ins) || INS_IsPredicated(ins))
        return NULL;
    /* IARG_MEMORYREAD_EA is the first memory operand that is read */
    UINT32 n = INS_MemoryOperandCount(ins), i = 0;
    while (i < n && !INS_MemoryOperandIsRead(ins, i)) i++;
    if (i == n || INS_MemoryOperandSize(ins, i) < sz)
        return NULL;
    switch (sz) {
    case 8: return (AFUNPTR)Load8;
    case 4: return (AFUNPTR)Load4;
    case 2: return (AFUNPTR)Load2;
    case 1: return (AFUNPTR)Load1;
    default: return NULL;
    }
}

/* IARG_REG_CONST_REFERENCE is a raw register-byte buffer in newer Pin kits.
 * These copies also preserve the old PIN_REGISTER qword layout on x86-64. */
static ADDRINT PIN_FAST_ANALYSIS_CALL XmmLo(const VOID *r)
{
    UINT64 value; memcpy(&value, r, sizeof value); return (ADDRINT)value;
}
static ADDRINT PIN_FAST_ANALYSIS_CALL XmmHi(const VOID *r)
{
    UINT64 value; memcpy(&value, static_cast<const UINT8 *>(r) + 8, sizeof value);
    return (ADDRINT)value;
}

/* ---------------------------------------------------- the PTWRITE sink (-sink ptwrite)
 * `ptwrite %rdi' = F3 REX.W 0F AE /4 with rm = rdi.  Written as bytes so the tool builds
 * with any assembler in the kit's tool chain. */
static bool g_ptw = false;

#define PTW_EMIT(v) __asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xe7" \
                                         : : "D" ((UINT64)(v)) : )

static VOID PIN_FAST_ANALYSIS_CALL PtwReg(ADDRINT v) { PTW_EMIT(v); }

static VOID PIN_FAST_ANALYSIS_CALL PtwMem(ADDRINT ea, UINT32 sz)
{
    UINT64 v = 0;
    PIN_SafeCopy(&v, (const VOID *)ea, sz);
    PTW_EMIT(v);
}

static VOID PIN_FAST_ANALYSIS_CALL PtwVal(ADDRINT v) { PTW_EMIT(v); }

static VOID PIN_FAST_ANALYSIS_CALL PtwXmmLo(const VOID *r)
{
    UINT64 v; memcpy(&v, r, sizeof v); PTW_EMIT(v);
}

static VOID PIN_FAST_ANALYSIS_CALL PtwXmmHi(const VOID *r)
{
    UINT64 v; memcpy(&v, static_cast<const UINT8 *>(r) + 8, sizeof v); PTW_EMIT(v);
}

/* ------------------------------------------------------------ instrumentation */
static UINT64 g_inserted = 0, g_skipped_after = 0;

/* an IT_MEM slice at a non-zero offset (JIT sites on wide vector operands): value -> g_scr */
static void emit_mem_off(INS ins, IPOINT ip, const Item &it)
{
    if (inline_loader(ins, it.off + 8)) {
        INS_InsertCall(ins, ip, (AFUNPTR)Load8Off, IARG_FAST_ANALYSIS_CALL, IARG_MEMORYREAD_EA,
                       IARG_ADDRINT, (ADDRINT)it.off, IARG_RETURN_REGS, g_scr, IARG_END);
        g_mem_inline++;
    } else {
        INS_InsertCall(ins, ip, (AFUNPTR)ReadMemOff, IARG_FAST_ANALYSIS_CALL, IARG_MEMORYREAD_EA,
                       IARG_ADDRINT, (ADDRINT)it.off, IARG_RETURN_REGS, g_scr, IARG_END);
        g_mem_safecopy++;
    }
}

static void emit_items(INS ins, IPOINT ip, const std::vector<Item> &items, bool ptw)
{
    for (size_t i = 0; i < items.size(); i++) {
        const Item &it = items[i];
        if (ptw) {
            /* one callout per value; program order is insertion order at the same IPOINT */
            switch (it.kind) {
            case IT_REG:
                INS_InsertCall(ins, ip, (AFUNPTR)PtwReg, IARG_FAST_ANALYSIS_CALL,
                               IARG_REG_VALUE, it.reg, IARG_END);
                break;
            case IT_FSBASE:
                INS_InsertCall(ins, ip, (AFUNPTR)PtwReg, IARG_FAST_ANALYSIS_CALL,
                               IARG_REG_VALUE, REG_SEG_FS_BASE, IARG_END);
                break;
            case IT_MEM:
                if (it.off) {
                    emit_mem_off(ins, ip, it);
                    INS_InsertCall(ins, ip, (AFUNPTR)PtwVal, IARG_FAST_ANALYSIS_CALL,
                                   IARG_REG_VALUE, g_scr, IARG_END);
                } else if (AFUNPTR ld = inline_loader(ins, it.size)) {
                    INS_InsertCall(ins, ip, ld, IARG_FAST_ANALYSIS_CALL, IARG_MEMORYREAD_EA,
                                   IARG_RETURN_REGS, g_scr, IARG_END);
                    INS_InsertCall(ins, ip, (AFUNPTR)PtwVal, IARG_FAST_ANALYSIS_CALL,
                                   IARG_REG_VALUE, g_scr, IARG_END);
                    g_mem_inline++;
                } else {
                    INS_InsertCall(ins, ip, (AFUNPTR)PtwMem, IARG_FAST_ANALYSIS_CALL,
                                   IARG_MEMORYREAD_EA, IARG_UINT32, it.size, IARG_END);
                    g_mem_safecopy++;
                }
                break;
            case IT_XMM_LO:
            case IT_XMM_HI:
                INS_InsertCall(ins, ip,
                               (AFUNPTR)(it.kind == IT_XMM_LO ? PtwXmmLo : PtwXmmHi),
                               IARG_FAST_ANALYSIS_CALL, IARG_REG_CONST_REFERENCE, it.reg,
                               IARG_END);
                break;
            }
            g_inserted++;
            continue;
        }
        switch (it.kind) {
        case IT_REG:
            INS_InsertFillBuffer(ins, ip, g_buf, IARG_REG_VALUE, it.reg, 0, IARG_END);
            break;
        case IT_FSBASE:
            INS_InsertFillBuffer(ins, ip, g_buf, IARG_REG_VALUE, REG_SEG_FS_BASE, 0, IARG_END);
            break;
        case IT_MEM:
            if (it.off) {
                emit_mem_off(ins, ip, it);
            } else if (AFUNPTR ld = inline_loader(ins, it.size)) {
                INS_InsertCall(ins, ip, ld, IARG_FAST_ANALYSIS_CALL, IARG_MEMORYREAD_EA,
                               IARG_RETURN_REGS, g_scr, IARG_END);
                g_mem_inline++;
            } else {
                INS_InsertCall(ins, ip, (AFUNPTR)ReadMem, IARG_FAST_ANALYSIS_CALL,
                               IARG_MEMORYREAD_EA, IARG_UINT32, it.size,
                               IARG_RETURN_REGS, g_scr, IARG_END);
                g_mem_safecopy++;
            }
            INS_InsertFillBuffer(ins, ip, g_buf, IARG_REG_VALUE, g_scr, 0, IARG_END);
            break;
        case IT_XMM_LO:
        case IT_XMM_HI:
            INS_InsertCall(ins, ip, (AFUNPTR)(it.kind == IT_XMM_LO ? XmmLo : XmmHi),
                           IARG_FAST_ANALYSIS_CALL, IARG_REG_CONST_REFERENCE, it.reg,
                           IARG_RETURN_REGS, g_scr, IARG_END);
            INS_InsertFillBuffer(ins, ip, g_buf, IARG_REG_VALUE, g_scr, 0, IARG_END);
            break;
        }
        g_inserted++;
    }
}

#include "jitplan.h"

static VOID PIN_FAST_ANALYSIS_CALL CountInc(UINT64 *p) { (*p)++; }

static VOID Instruction(INS ins, VOID *)
{
    std::map<ADDRINT, const Site *>::const_iterator it = g_sites.find(INS_Address(ins));
    Site dynamic;
    const Site *s = it == g_sites.end() ? NULL : it->second;
    if (!s && KnobJitBridge.Value() && jit_site(INS_Address(ins), &dynamic)) s = &dynamic;
    if (!s) return;
    /* the sink of this site: all-PTWRITE, or (mixed) PTWRITE unless JIT or over the budget */
    bool ptw = g_ptw;
    if (g_mixed)
        ptw = (s != &dynamic) && !g_bufaddr.count(INS_Address(ins));
    if (g_count && s != &dynamic) {
        std::map<ADDRINT, CountRec *>::iterator c = g_counts.find(INS_Address(ins));
        if (c != g_counts.end())
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)CountInc, IARG_FAST_ANALYSIS_CALL,
                           IARG_PTR, &c->second->n, IARG_END);
    }
    if (!s->before.empty())
        emit_items(ins, IPOINT_BEFORE, s->before, ptw);
    if (!s->after.empty()) {
        if (INS_HasFallThrough(ins))
            emit_items(ins, IPOINT_AFTER, s->after, ptw);
        else
            g_skipped_after += s->after.size();
    }
}

static VOID ImageLoad(IMG img, VOID *)
{
    jit_image(img);
    const std::string name = IMG_Name(img);
    std::map<std::string, std::map<ADDRINT, Site> >::iterator p = g_plan.find(name);
    if (p == g_plan.end()) {
        /* fall back on the basename: LD_LIBRARY_PATH shadow trees and /proc paths differ */
        size_t sl = name.rfind('/');
        const std::string base = (sl == std::string::npos) ? name : name.substr(sl + 1);
        for (p = g_plan.begin(); p != g_plan.end(); ++p) {
            size_t s2 = p->first.rfind('/');
            if ((s2 == std::string::npos ? p->first : p->first.substr(s2 + 1)) == base)
                break;
        }
        if (p == g_plan.end())
            return;
    }
    const ADDRINT off = IMG_LoadOffset(img);
    for (std::map<ADDRINT, Site>::const_iterator s = p->second.begin();
         s != p->second.end(); ++s) {
        const ADDRINT rt = s->first + off;
        g_sites[rt] = &s->second;
        g_resolved++;
        if (g_mixed) {
            if (g_bufsites.count(std::make_pair(base_of(p->first), s->first))) {
                g_bufaddr.insert(rt); g_buf_sites++;
            } else {
                g_ptw_sites++;
            }
        }
        if (g_count && !g_counts.count(rt)) {
            CountRec *c = new CountRec;
            c->img = base_of(p->first); c->link = s->first; c->n = 0;
            c->nval = (UINT32)(s->second.before.size() + s->second.after.size());
            g_counts[rt] = c;
        }
    }
    g_images++;
    fprintf(stderr, "[hifitool] image %s base+%lx: %lu addresses\n", name.c_str(),
            (unsigned long)off, (unsigned long)p->second.size());
    PIN_RemoveInstrumentation();      /* traces cached before this image loaded */
}

static void write_counts()
{
    char path[1024];
    snprintf(path, sizeof(path), "%s.%d.tsv", KnobCountOut.Value().c_str(), (int)PIN_GetPid());
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "[hifitool] cannot write %s\n", path); return; }
    fprintf(f, "# wall_s %.6f\n", now_s() - g_t0);
    for (std::map<ADDRINT, CountRec *>::const_iterator c = g_counts.begin(); c != g_counts.end(); ++c)
        if (c->second->n)
            fprintf(f, "%s\t%lx\t%u\t%llu\n", c->second->img.c_str(), (unsigned long)c->second->link,
                    c->second->nval, (unsigned long long)c->second->n);
    fclose(f);
}

static VOID ForkChild(THREADID, const CONTEXT *, VOID *)
{
    /* a forked child counts (and times) its own execution only */
    for (std::map<ADDRINT, CountRec *>::iterator c = g_counts.begin(); c != g_counts.end(); ++c)
        c->second->n = 0;
    g_t0 = now_s();
}

static VOID Fini(INT32, VOID *)
{
    if (g_count)
        write_counts();
    if (g_mixed)
        fprintf(stderr, "[hifitool] mixed sink: %lu ELF-plan addresses PTWRITE, %lu buffer (+ JIT sites: buffer)\n",
                (unsigned long)g_ptw_sites, (unsigned long)g_buf_sites);
    if (!KnobStats.Value())
        return;
    UINT64 tot = 0;
    for (size_t i = 0; i < g_all.size(); i++)
        tot += g_all[i]->nvalues;
    fprintf(stderr, "[hifitool] threads=%lu values=%lu bytes=%lu addresses=%lu "
                    "inserted=%lu skipped_after=%lu\n",
            (unsigned long)g_all.size(), (unsigned long)tot, (unsigned long)(tot * 8),
            (unsigned long)g_resolved, (unsigned long)g_inserted,
            (unsigned long)g_skipped_after);
    fprintf(stderr, "[hifitool] memread=%s mem_inline=%lu mem_safecopy=%lu mem_faults=%lu\n",
            g_memload ? "load" : "safecopy", (unsigned long)g_mem_inline,
            (unsigned long)g_mem_safecopy, (unsigned long)g_mem_faults);
    fprintf(stderr, "[hifitool] jit_added=%llu jit_moved=%llu jit_removed=%llu jit_requested=%llu\n",
            (unsigned long long)g_jitadded, (unsigned long long)g_jitmoved,
            (unsigned long long)g_jitremoved, (unsigned long long)g_jitrequested);
    fprintf(stderr, "[hifitool] jit_batches=%llu (stopped %llu) jit_ranges=%llu jit_keymiss=%llu (-jitbatch %d)\n", (unsigned long long)g_jitbatches, (unsigned long long)g_jitstopped,
            (unsigned long long)g_jitranges, (unsigned long long)g_jitkeymiss, (int)KnobJitBatch.Value());
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) {
        fprintf(stderr, "usage: pin -t hifitool.so -plan PLAN [-cvdir DIR] -- <prog>\n");
        return 1;
    }
    if (KnobPlan.Value().empty() && !KnobJitBridge.Value()) {
        fprintf(stderr, "[hifitool] -plan or -jitbridge is required\n");
        return 1;
    }
    if (KnobPlan.Value().empty())
        fprintf(stderr, "[hifitool] no ELF PLAN: only runtime-published generated-code "
                        "sites will be instrumented\n");
    PIN_InitSymbols();
    if (!KnobPlan.Value().empty()) load_plan(KnobPlan.Value());
    if (KnobSink.Value() != "buffer" && KnobSink.Value() != "ptwrite" && KnobSink.Value() != "count") {
        fprintf(stderr, "[hifitool] -sink must be buffer, ptwrite or count\n");
        return 1;
    }
    g_ptw = (KnobSink.Value() == "ptwrite");
    g_count = (KnobSink.Value() == "count");
    g_t0 = now_s();
    if (!KnobPtwBuffer.Value().empty()) {
        if (!g_ptw) {
            fprintf(stderr, "[hifitool] -ptwbuffer needs -sink ptwrite\n");
            return 1;
        }
        FILE *f = fopen(KnobPtwBuffer.Value().c_str(), "r");
        if (!f) {
            fprintf(stderr, "[hifitool] cannot open %s\n", KnobPtwBuffer.Value().c_str());
            return 1;
        }
        char line[4096], img[4000];
        unsigned long a;
        while (fgets(line, sizeof(line), f))
            if (line[0] != '#' && sscanf(line, "%3999s %lx", img, &a) == 2)
                g_bufsites.insert(std::make_pair(std::string(img), (ADDRINT)a));
        fclose(f);
        g_mixed = true;
        fprintf(stderr, "[hifitool] mixed sink: %lu ELF-plan sites on the buffer (%s)\n",
                (unsigned long)g_bufsites.size(), KnobPtwBuffer.Value().c_str());
    }
    if (KnobMemRead.Value() != "load" && KnobMemRead.Value() != "safecopy") {
        fprintf(stderr, "[hifitool] -memread must be load or safecopy\n");
        return 1;
    }
    g_memload = (KnobMemRead.Value() == "load");
    if (g_memload)
        PIN_AddInternalExceptionHandler(LoadFault, 0);
    /* in PTWRITE mode the values travel in the Intel PT packet stream, so there is no
     * cv.<pid>.<tid>.bin to write and no fill buffer to drain */
    g_discard = (g_ptw && !g_mixed) || (KnobCvDir.Value() == "/dev/null");
    PIN_InitLock(&g_lock);
    PIN_InitLock(&g_jitlock);
    g_tls = PIN_CreateThreadDataKey(NULL);
    g_scr = PIN_ClaimToolRegister();
    if (!REG_valid(g_scr)) {
        fprintf(stderr, "[hifitool] no scratch register available\n");
        return 1;
    }
    g_buf = PIN_DefineTraceBuffer(8, KnobPages.Value(), BufferFull, 0);
    if (g_buf == BUFFER_ID_INVALID) {
        fprintf(stderr, "[hifitool] PIN_DefineTraceBuffer failed\n");
        return 1;
    }
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);
    IMG_AddInstrumentFunction(ImageLoad, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    if (KnobJitBridge.Value() && KnobJitBatch.Value()) {
        if (PIN_SpawnInternalThread(JitBatchThread, 0, 0, NULL) == INVALID_THREADID) {
            fprintf(stderr, "[hifitool] cannot start the JIT hand-over thread\n");
            return 1;
        }
        PIN_AddPrepareForFiniFunction(JitBatchStop, 0);
    }
    PIN_AddFiniFunction(Fini, 0);
    if (g_count)
        PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, ForkChild, 0);
    PIN_StartProgram();
    return 0;
}
