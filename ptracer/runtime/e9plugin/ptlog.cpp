/*
 * ptlog.cpp -- PTracer Stage-2 E9Tool plugin: NAKED critical-value logging
 * trampolines.
 *
 * E9Tool's ordinary `call' trampolines into a C handler pay for argument
 * marshalling, call/ret and the callee-save discipline on every logged value,
 * and the only thing such a handler would do is one `ptwrite'.  This plugin
 * emits that `ptwrite' (or a buffer store, or a counter bump) *inline* in the
 * trampoline instead -- no call, no register save except the scratch a
 * particular site actually needs.
 *
 * BUILD
 *   g++ -std=c++17 -fPIC -shared -O2 -o ptlog.so ptlog.cpp \
 *       -I <e9patch>/src/e9tool
 *   (make -C runtime/e9plugin does this and builds the runtimes too)
 *
 * USE
 *   e9tool --plugin=ptlog.so:--sites=SITES.txt \
 *          -M 'plugin(ptlog.so).match()' \
 *          -P 'replace plugin(ptlog.so).patch()' \
 *          input.bin -o output.bin
 *
 *   The `replace` position is essential: it makes E9Tool hand the *whole*
 *   trampoline body to the plugin (E9Tool then only appends "$BREAK"), so the
 *   plugin controls what runs before and after the displaced original
 *   instruction.  The plugin emits "$instr" itself at the right point.
 *   (e9tool.cpp builds the composite trampoline; a POS_REPLACE patch
 *   suppresses E9Tool's own "$instr".)
 *
 * SITE FILE FORMAT (produced by rewrite.py from a spec v2 JSON)
 *   Lines (blank lines and '#' comments ignored):
 *     sink   ptwrite|buffer
 *     space  N                  # min non-PTWRITE instructions between PTWRITEs
 *     sync   N                  # buffer sink: PTWRITE a running count every N values (0=off)
 *     bufoff N                  # buffer sink: %gs slot base (default 0)
 *     delta  0                  # accepted for compatibility; must be 0
 *     slotbase ADDR             # accepted for compatibility (== counterbase)
 *     counterbase ADDR          # link-time base of the keyframe countdown counters
 *     nslots 0 / ncounters N    # how many cells the reserved region holds
 *     liveness 0|1              # honour the `:S<mask>' fields (default 1)
 *     gt 1                      # also patch every memory-accessing instruction
 *     gtoff N / gtstack 0|1     # ... %gs offset of the gt cursor; stack forms too
 *     gtskip ADDR               # ... but emit no gt sequence at this address
 *     <hexaddr> <op>[,<op>]...  # one line per instrumented instruction
 *   where <op> is  <when>:<kind>:<arg>:<siteid>[:D|:L][:R<K>][:N][:S<mask>]
 *     when  = B (before the original instruction) | A (after)
 *     kind  = r (log a register: arg = rax..r15 / xmm0..xmm15 / fs_base)
 *           = m (log the memory operand's value: arg = access size in bytes)
 *           = i (log a COMPILE-TIME CONSTANT: arg = the value, decimal or 0x..)
 *               The value is stored straight into the buffer slot as
 *               `movq $imm32,disp(%cur)' (REX.W C7 /0), so an immediate op
 *               needs NO scratch beyond the cursor every buffer-sink site
 *               already takes.  Buffer sink only: `ptwrite' has no immediate
 *               form.  Used by `rewrite.py --log-blocks' to log a BASIC-BLOCK
 *               IDENTIFIER at every block leader, which is how control flow
 *               is recorded when Intel PT is not used.
 *     siteid= the spec v2 site id (echoed into the site map)
 *     D | L = the spec says EFLAGS is dead | live here
 *     R<K>  = a `resync' site: log unconditionally every K-th execution
 *     N     = a value that must never be wrapped in a guard (a constant)
 *     S<M>  = bitmask of the 64-bit GP registers that are DEAD here (bit i =
 *             x86 encoding i), usable as scratch without push/pop
 *   Ops are emitted in file order; rewrite.py is responsible for the SPEC_FORMAT
 *   ordering rule (all `before` sites by ascending id, the instruction, then all
 *   `after` sites by ascending id).
 *
 * OUTPUT
 *   With --sitemap=FILE the plugin writes one CSV line per emitted logging
 *   instruction: addr,siteid,when,kind,arg,byte_offset_in_trampoline_body.
 *   rewrite.py turns this plus a disassembly of the rewritten binary into the
 *   SPEC_FORMAT section-2 site map JSON.
 *
 * CORRECTNESS NOTES
 *  - FLAGS: an instruction that writes EFLAGS (add/sub/cmp/dec/incq) is only
 *    emitted where the site file says the flags are dead (`:D', re-verified by
 *    rewrite.py); everywhere else `lea' does the arithmetic and `jrcxz' the
 *    test.
 *  - RED ZONE: a site that needs a pushed scratch register does
 *      lea -0x80(%rsp),%rsp ; push ... ; ... ; pop ... ; lea 0x80(%rsp),%rsp
 *    so the 128-byte System V red zone below the original %rsp is never written.
 *    Memory operands that use %rsp as base/index have their displacement
 *    corrected by the exact amount %rsp was moved.
 *  - PT FIFO: consecutive PTWRITEs must be separated by >= `space` (default 3)
 *    cheap non-PTWRITE instructions, otherwise the on-chip PT FIFO overflows.
 *    The emitter tracks the distance and pads with 1-byte `nop`s.  Across two
 *    trampolines for adjacent instructions the separation is the relocated
 *    instruction plus two jumps = exactly 3, which meets the rule but has no
 *    margin; raise `space` if a dense region shows OVF > 0.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include "../rt/ptlog_abi.h"
#include "../rt/ptgt_format.h"

#include <sys/mman.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <getopt.h>

#include "e9plugin.h"

using namespace e9tool;

/****************************************************************************/
/* Site table                                                               */
/****************************************************************************/

enum Sink { SINK_PTWRITE, SINK_BUFFER };

struct Op
{
    bool     after;     // false = before the original instruction
    char     kind;      // 'r' = register, 'm' = memory operand, 'i' = immediate
    int      reg;       // kind=='r': 0..15 GP, 16..31 = xmm0..xmm15
    int      size;      // kind=='m': 1/2/4/8
    long     imm;       // kind=='i': the constant logged (must fit in int32)
    long     site;      // spec v2 site id
    bool     dead;      // the spec's `flags_dead' for this site
    // KEYFRAME logging.  `resync' marks a spec `resync' site (a loop back-edge
    // header re-anchor): its registers are logged UNCONDITIONALLY, but only
    // every `kf'-th execution of the site.  kf == 0 means no keyframe.
    bool     resync;
    long     kf;
    // LIVENESS (site-file field `:S<mask>'): bit i is set when 64-bit GP
    // register i (x86 encoding order, never %rsp) is DEAD at this op's program
    // point, so a trampoline may clobber it without saving it.
    unsigned dregs;
    // `:N' (rewrite.py, `noguard'): a value that must never be wrapped in a
    // guard whatever its flags say (a constant).  Recorded; the flag-free
    // forms are chosen from `dead' alone.
    bool     noguard;
};

struct Site
{
    std::vector<Op> ops;
};

static std::map<intptr_t, Site>  sites;
static Sink   opt_sink   = SINK_PTWRITE;
static int    opt_space  = 3;
static long   opt_sync   = 4096;
// `synccarrier tnt': the sync marker carries its count
// in TNT bits instead of a PTWRITE payload, so a buffer-sink image executes no
// PTWRITE at all.  Default off = `ptwrite'.
static bool   opt_sync_tnt = false;
static int    opt_gsbase = 0;       // %gs slot base
static FILE  *opt_map    = nullptr;
static intptr_t opt_slotbase = 0;   // accepted for compatibility (== counterbase)
static long   n_emitted  = 0;       // logging instructions emitted
static long   n_patched  = 0;       // instructions patched
// KEYFRAME logging.  A second array of 8-byte cells in the same reserved
// region holds one COUNTDOWN COUNTER per keyframe guard.  The cells are
// initialised to 1 (not 0), so the first execution of a guard logs and re-arms
// with K: a resync site anchors its registers immediately and then every K
// executions.  A cell lives in the image's read-write data region, i.e. it is
// ONE CELL SHARED BY EVERY THREAD: on the PTWRITE sink that costs at most a
// keyframe interval (each value is self-locating in the packet stream); on the
// BUFFER sink, whose cv cursor is POSITIONAL, keyframes are for single-threaded
// targets only.
static intptr_t opt_counterbase = 0;
static long   opt_nslots    = -1;   // region sizing, from rewrite.py (-1 = derive)
static long   opt_ncounters = -1;
static long   n_counters  = 0;      // keyframe counters handed out
// GROUND-TRUTH ADDRESS LOGGING.  With `gt 1'
// in the site file the plugin patches EVERY memory-accessing instruction it
// can re-encode -- not only the spec's critical-value sites -- and each such
// trampoline additionally stores the 16-byte record
//     { effective address of the original instruction, its original ip }
// through a per-thread cursor at %gs:opt_gtoff (runtime/rt/ptlogrt.c).  An
// instruction that is BOTH a critical-value site and a gt site is patched
// ONCE, with both sequences in the same trampoline: E9Patch patches an
// instruction once, and the two logs are independent streams.
//
// GT sequences, including compact REP STOS descriptors, emit no PTWRITE,
// conditional branch or indirect transfer. Guard faults and timing packets
// can still change PT traffic; accuracy captures are not overhead captures.
static bool   opt_gt       = false; // patch every memory-accessing instruction
static bool   opt_gt_stack = true;  // ... including push/pop/call/ret/leave
static int    opt_gtoff    = 512;   // %gs offset of the gt cursor
static long   n_gt_sites   = 0;     // gt trampolines emitted
static long   n_gt_mem     = 0;     // ... from an explicit memory operand
static long   n_gt_stack   = 0;     // ... from an implicit stack operand
static long   n_gt_skip    = 0;     // memory-accessing instructions refused
// GT-SKIP LIST: addresses at which NO gt sequence is emitted even under
// `gt 1'.  A gt patch
// on a critical-value site's NEIGHBOUR can steal the successor / rel8 landing
// instruction E9Patch's single-byte tactic (T3b) needs for the site itself, so
// rewrite.py --gt-retry gives up the ground truth at a few gt-only neighbours
// rather than the critical value.  A skipped instruction is `excluded' in the
// comparison (it records no ground truth); it changes nothing the site logs.
static std::set<intptr_t> gt_skip;
static long   n_gt_skipped = 0;     // gt candidates dropped by the skip list
static bool   opt_liveness = true;  // use the site file's dead-register masks
static long   n_scr_dead  = 0;      // scratch registers taken from `dead_regs'
static long   n_scr_push  = 0;      // scratch registers that had to be pushed
static long   n_scr_borrow = 0;    // ... of those, ones the site itself LOGS
static long   n_site_free = 0;      // patched instructions with NO push/pop
static long   n_site_push = 0;      // ... and with at least one
static long   n_kf_resync = 0;      // resync sites emitted behind a keyframe counter

// %gs layout used by the buffer sink (see runtime/rt/ptlogrt.c):
//   gs:[B+0]  = cursor  (next 8-byte slot to write)
//   gs:[B+8]  = countdown to the next sync marker
//   gs:[B+16] = running total of values written by this thread
#define GS_CURSOR   (opt_gsbase + 0)
#define GS_COUNT    (opt_gsbase + 8)
#define GS_TOTAL    (opt_gsbase + 16)

static const char *GP_NAMES[16] =
{
    "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
    "r8","r9","r10","r11","r12","r13","r14","r15"
};

/*
 * Register name -> encoding.  0..15 GP (x86 encoding order), 16+n = xmm n.
 * Returns -1 if unknown.
 */
#define REG_FS_BASE     32
#define REG_GS_BASE     33

static int parseReg(const char *s)
{
    for (int i = 0; i < 16; i++)
        if (strcmp(s, GP_NAMES[i]) == 0)
            return i;
    if (strncmp(s, "xmm", 3) == 0)
    {
        char *end = nullptr;
        long n = strtol(s+3, &end, 10);
        if (end != nullptr && *end == '\0' && n >= 0 && n <= 15)
            return 16 + (int)n;
    }
    if (strcmp(s, "fs_base") == 0)
        return REG_FS_BASE;
    if (strcmp(s, "gs_base") == 0)
        return REG_GS_BASE;
    return -1;
}

static const char *regName(int r)
{
    static char buf[16];
    if (r < 16)
        return GP_NAMES[r];
    if (r == REG_FS_BASE) return "fs_base";
    if (r == REG_GS_BASE) return "gs_base";
    snprintf(buf, sizeof(buf), "xmm%d", r-16);
    return buf;
}

static void parseSiteFile(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (f == nullptr)
        error("failed to open site file \"%s\"", filename);
    char line[64*1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), f) != nullptr)
    {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        char *nl = strchr(p, '\n');
        if (nl != nullptr) *nl = '\0';

        if (strncmp(p, "sink", 4) == 0)
        {
            if (strstr(p, "buffer") != nullptr) opt_sink = SINK_BUFFER;
            else if (strstr(p, "ptwrite") != nullptr) opt_sink = SINK_PTWRITE;
            else error("%s:%d: bad sink", filename, lineno);
            continue;
        }
        if (strncmp(p, "gtoff", 5) == 0)  { opt_gtoff = (int)strtol(p+5, nullptr, 0); continue; }
        if (strncmp(p, "gtstack", 7) == 0) { opt_gt_stack = (strtol(p+7, nullptr, 0) != 0); continue; }
        if (strncmp(p, "gtskip", 6) == 0)
            { gt_skip.insert((intptr_t)strtoull(p+6, nullptr, 0)); continue; }
        if (strncmp(p, "gt", 2) == 0 && (p[2] == ' ' || p[2] == '\t'))
            { opt_gt = (strtol(p+2, nullptr, 0) != 0); continue; }
        if (strncmp(p, "liveness", 8) == 0)
            { opt_liveness = (strtol(p+8, nullptr, 0) != 0); continue; }
        if (strncmp(p, "space", 5) == 0)  { opt_space  = (int)strtol(p+5, nullptr, 0); continue; }
        if (strncmp(p, "synccarrier", 11) == 0)
        {
            if (strstr(p+11, "tnt") != nullptr) opt_sync_tnt = true;
            else if (strstr(p+11, "ptwrite") != nullptr) opt_sync_tnt = false;
            else error("%s:%d: bad synccarrier", filename, lineno);
            continue;
        }
        if (strncmp(p, "sync", 4) == 0)   { opt_sync   = strtol(p+4, nullptr, 0); continue; }
        if (strncmp(p, "bufoff", 6) == 0) { opt_gsbase = (int)strtol(p+6, nullptr, 0); continue; }
        if (strncmp(p, "delta", 5) == 0)
        {
            if (strtol(p+5, nullptr, 0) != 0)
                error("%s:%d: log-on-change guards (`delta') are not "
                    "supported by this plugin", filename, lineno);
            continue;
        }
        if (strncmp(p, "slotbase", 8) == 0)
            { opt_slotbase = (intptr_t)strtoull(p+8, nullptr, 0); continue; }
        if (strncmp(p, "counterbase", 11) == 0)
            { opt_counterbase = (intptr_t)strtoull(p+11, nullptr, 0); continue; }
        if (strncmp(p, "nslots", 6) == 0)
            { opt_nslots = strtol(p+6, nullptr, 0); continue; }
        if (strncmp(p, "ncounters", 9) == 0)
            { opt_ncounters = strtol(p+9, nullptr, 0); continue; }

        // <hexaddr> <op>[,<op>]...
        char *sp = strchr(p, ' ');
        if (sp == nullptr)
            error("%s:%d: expected `<addr> <ops>'", filename, lineno);
        *sp = '\0';
        intptr_t addr = (intptr_t)strtoull(p, nullptr, 0);
        Site site;
        char *tok = sp+1;
        while (*tok != '\0')
        {
            char *comma = strchr(tok, ',');
            if (comma != nullptr) *comma = '\0';
            // <when>:<kind>:<arg>:<site>
            char when = tok[0];
            if ((when != 'B' && when != 'A') || tok[1] != ':')
                error("%s:%d: bad op \"%s\"", filename, lineno, tok);
            char kind = tok[2];
            if ((kind != 'r' && kind != 'm' && kind != 'i') || tok[3] != ':')
                error("%s:%d: bad op \"%s\"", filename, lineno, tok);
            char *arg = tok+4;
            char *colon = strchr(arg, ':');
            if (colon == nullptr)
                error("%s:%d: bad op \"%s\"", filename, lineno, tok);
            *colon = '\0';
            Op op;
            op.after = (when == 'A');
            op.kind  = kind;
            op.reg   = -1;
            op.size  = 0;
            op.imm   = 0;
            op.dead  = false;
            op.resync = false;
            op.kf    = 0;
            op.dregs = 0;
            op.noguard = false;
            // <site>[:D|:L][:R<K>][:N][:S<M>]
            //   D / L  the spec says EFLAGS is dead / live here
            //   R<K>   a `resync' site: log unconditionally every K-th execution
            //   N      a value that must never be guarded
            //   S<M>   bitmask of the 64-bit GP registers that are DEAD here
            for (char *flag = strchr(colon+1, ':'); flag != nullptr; )
            {
                *flag = '\0';
                char *next = strchr(flag+1, ':');
                switch (flag[1])
                {
                    case 'D': op.dead = true;  break;
                    case 'L': op.dead = false; break;
                    case 'R': op.resync = true;
                              op.kf = strtol(flag+2, nullptr, 0); break;
                    case 'S': op.dregs = (unsigned)strtoul(flag+2, nullptr, 0)
                                         & 0xffffu & ~(1u << 4); break;
                    case 'N': op.noguard = true; break;
                    default:
                        error("%s:%d: unknown op field \"%s\"", filename,
                            lineno, flag+1);
                }
                flag = next;
            }
            op.site  = strtol(colon+1, nullptr, 0);
            if (kind == 'r')
            {
                op.reg = parseReg(arg);
                if (op.reg < 0)
                    error("%s:%d: unknown register \"%s\"", filename, lineno, arg);
                /* %rsp IS loggable: `ptwrite %rsp' (f3 48 0f ae e4) is a valid
                 * encoding, and the analyzer anchors %rsp at every function
                 * entry, so refusing it made every spec un-rewritable.  Both
                 * sinks log the value %rsp has at the ORIGINAL instruction:
                 * the PTWRITE sink emits `ptwrite %rsp' at a point where the
                 * trampoline has already restored %rsp, the buffer sink
                 * materialises it with `lea rsp_delta(%rsp),%r10' (see
                 * emitBufOpReg) because its prologue has moved %rsp. */
                if (op.reg == REG_GS_BASE)
                    error("%s:%d: `gs_base' is not loggable: on x86-64 Linux "
                        "%%gs:0 holds no TCB self-pointer (glibc puts the TCB "
                        "under %%fs), reading the GS base needs `rdgsbase' "
                        "(CR4.FSGSBASE) or arch_prctl(2), and this rewriter's "
                        "buffer sink owns %%gs.  Use `fs_base', or have the "
                        "analyzer drop the site.", filename, lineno);
            }
            else if (kind == 'i')
            {
                /* A CONSTANT.  Stored with
                 * `movq $imm32,disp(%cur)', whose immediate is SIGN-EXTENDED to
                 * 64 bits, so the value must fit in a signed 32-bit field --
                 * block identifiers are a dense range from 0 and always do. */
                errno = 0;
                char *end = nullptr;
                long v = strtol(arg, &end, 0);
                if (end == arg || *end != '\0' || errno != 0 ||
                        v < INT32_MIN || v > INT32_MAX)
                    error("%s:%d: bad immediate \"%s\" (must be a signed "
                        "32-bit integer)", filename, lineno, arg);
                op.imm = v;
                op.noguard = true;
            }
            else
            {
                op.size = (int)strtol(arg, nullptr, 0);
                if (op.size != 1 && op.size != 2 && op.size != 4 && op.size != 8)
                    error("%s:%d: bad memop size %d", filename, lineno, op.size);
            }
            site.ops.push_back(op);
            if (comma == nullptr) break;
            tok = comma+1;
        }
        auto r = sites.insert({addr, site});
        if (!r.second)
            error("%s:%d: duplicate address 0x%" PRIxPTR, filename, lineno, addr);
    }
    fclose(f);
}

/****************************************************************************/
/* Machine-code emitter (into a byte buffer; macros/rel32 handled separately) */
/****************************************************************************/

/*
 * The trampoline body is a sequence of "chunks".  A chunk is either a run of
 * literal bytes, the "$instr" macro, or a rip-relative-target rel32 field.
 * We build it as a small vector so that "$instr" and {"rel32":...} can be
 * interleaved with raw bytes when we print the E9Patch template.
 */
enum ChunkKind { CH_BYTES, CH_INSTR, CH_REL32 };
struct Chunk
{
    ChunkKind  kind;
    intptr_t   target;              // CH_REL32
    std::vector<uint8_t> bytes;     // CH_BYTES
};

struct Emitter
{
    std::vector<Chunk> chunks;
    int  since_ptwrite = 1000;      // instructions since the last PTWRITE
    long body_len      = 0;         // bytes emitted so far (rel8 arithmetic)
    // Byte offsets (within the body) of the instructions that actually put a
    // value into the log, and of the buffer sink's periodic sync markers.
    // rewrite.py turns these into exact site-map addresses instead of
    // re-discovering them by disassembly heuristics.
    std::vector<long> log_offs;
    // Per logged value, the KEYFRAME counter guard that wraps it (or 0 / -1):
    //   log_cnt    link-time address of its 8-byte countdown counter
    //   log_kfper  the period K
    //   log_kfbr   body offset of the `jnz' that skips the whole logging path
    //   log_kfjoin body offset of that `jnz's target -- seeing it as the next
    //              instruction means this execution was NOT a keyframe
    std::vector<long> log_cnt, log_kfper, log_kfbr, log_kfjoin;
    std::vector<long> sync_offs;
    // `synccarrier tnt' only: the marker's TNT-loop roles (s = start, b = the
    // bit branch, z = the not-taken path of it); the END of each marker is its
    // `sync_offs' entry.  Always empty for the PTWRITE carrier.
    std::vector<long> tnt_offs;
    std::vector<char> tnt_role;

    void pushLog(long off)
    {
        log_offs.push_back(off);
        log_cnt.push_back(0);
        log_kfper.push_back(0);
        log_kfbr.push_back(-1);
        log_kfjoin.push_back(-1);
    }

    void raw(std::initializer_list<int> bs)
    {
        if (chunks.empty() || chunks.back().kind != CH_BYTES)
            chunks.push_back(Chunk{CH_BYTES, 0, {}});
        for (int b: bs)
        {
            chunks.back().bytes.push_back((uint8_t)b);
            body_len++;
        }
    }
    void raw(const std::vector<uint8_t> &bs)
    {
        if (chunks.empty() || chunks.back().kind != CH_BYTES)
            chunks.push_back(Chunk{CH_BYTES, 0, {}});
        for (uint8_t b: bs)
        {
            chunks.back().bytes.push_back(b);
            body_len++;
        }
    }
    void imm32(int32_t v)
    {
        raw({v & 0xff, (v>>8) & 0xff, (v>>16) & 0xff, (v>>24) & 0xff});
    }
    void instr()                    // the displaced original instruction
    {
        chunks.push_back(Chunk{CH_INSTR, 0, {}});
        since_ptwrite++;            // conservatively count it as one instruction
        // body_len is unknown for $instr; only used for rel8 spans that never
        // cross it (asserted by construction).
    }
    void rel32(intptr_t target)
    {
        chunks.push_back(Chunk{CH_REL32, target, {}});
        body_len += 4;
    }
    void tick(int n = 1) { since_ptwrite += n; }
};

/* --- primitive instructions (none of these touch EFLAGS) ---------------- */

static inline int rexFor(bool w, int r, int x, int b)
{
    int rex = (w? 0x48: 0x40);
    if (r >= 8) rex |= 0x4;
    if (x >= 8) rex |= 0x2;
    if (b >= 8) rex |= 0x1;
    return rex;
}

// nop
static void emitNop(Emitter &e) { e.raw({0x90}); e.tick(); }

// push %r / pop %r   (r = 0..15)
static void emitPush(Emitter &e, int r)
{
    if (r >= 8) e.raw({0x41});
    e.raw({0x50 + (r & 7)});
    e.tick();
}
static void emitPop(Emitter &e, int r)
{
    if (r >= 8) e.raw({0x41});
    e.raw({0x58 + (r & 7)});
    e.tick();
}

// lea disp32(%rsp),%rsp
static void emitLeaRsp(Emitter &e, int32_t d)
{
    e.raw({0x48, 0x8d, 0xa4, 0x24});
    e.imm32(d);
    e.tick();
}

// lea disp8(%r),%r   (r = 0..15)
static void emitLeaReg(Emitter &e, int r, int8_t d)
{
    e.raw({rexFor(true, r, 0, r), 0x8d, 0x40 | ((r&7)<<3) | (r&7)});
    if ((r & 7) == 4) e.raw({0x24});        // SIB needed for rsp/r12
    e.raw({(int)(uint8_t)d});
    e.tick();
}

// lea disp32(%r),%r  (uses the 3-bytes-shorter disp8 form when it fits)
static void emitLeaReg32(Emitter &e, int r, int32_t d)
{
    if (d >= -128 && d <= 127) { emitLeaReg(e, r, (int8_t)d); return; }
    e.raw({rexFor(true, r, 0, r), 0x8d, 0x80 | ((r&7)<<3) | (r&7)});
    if ((r & 7) == 4) e.raw({0x24});
    e.imm32(d);
    e.tick();
}


// lea disp32(%rsp),%dst   (dst != rsp; used to materialise the ORIGINAL %rsp
// after the trampoline prologue has moved it)
static void emitLeaFromRsp(Emitter &e, int dst, int32_t d)
{
    e.raw({rexFor(true, dst, 0, 4), 0x8d, 0x80 | ((dst&7)<<3) | 0x04, 0x24});
    e.imm32(d);
    e.tick();
}


// mov %seg:disp32,%dst   /  mov %src,%gs:disp32   (seg = 0x64 fs, 0x65 gs)
static void emitMovFromSeg(Emitter &e, int seg, int dst, int32_t off)
{
    e.raw({seg, rexFor(true, dst, 0, 0), 0x8b, 0x04 | ((dst&7)<<3), 0x25});
    e.imm32(off);
    e.tick();
}
static void emitMovFromGs(Emitter &e, int dst, int32_t off)
{
    emitMovFromSeg(e, 0x65, dst, off);
}
static void emitMovToGs(Emitter &e, int src, int32_t off)
{
    e.raw({0x65, rexFor(true, src, 0, 0), 0x89, 0x04 | ((src&7)<<3), 0x25});
    e.imm32(off);
    e.tick();
}


// mov %src,disp(%base)
static void emitMovToMemDisp(Emitter &e, int base, int src, int32_t disp)
{
    // (%rbp)/(%r13) have no mod=00 encoding (rm=101 means rip-relative), so
    // they use mod=01 with a zero disp8.
    int mod;
    if (disp == 0 && (base & 7) != 5)       mod = 0;
    else if (disp >= -128 && disp <= 127)   mod = 1;
    else                                    mod = 2;
    e.raw({rexFor(true, src, 0, base), 0x89,
           (mod<<6) | ((src&7)<<3) | (base&7)});
    if ((base & 7) == 4) e.raw({0x24});     // (%rsp)/(%r12) need a SIB byte
    if (mod == 1)      e.raw({(int)(uint8_t)(int8_t)disp});
    else if (mod == 2) e.imm32(disp);
    e.tick();
}

/*
 * movq $imm32,disp(%base)  --  REX.W C7 /0 id, the immediate SIGN-EXTENDED to
 * 64 bits.  Writes no flags and needs no source register, so an `i' op costs
 * ONE instruction and no extra scratch: a block-leader log is the cursor load,
 * this store, and the cursor bump.
 *
 * The addressing bytes are byte-for-byte emitMovToMemDisp's with reg field 0,
 * which is what lets rt/ptlogrt.c's guard-page decoder (`ptlog_store_base')
 * recover the base register and displacement from the same fields -- it just
 * has to accept the C7 opcode as well (it does; see that function).
 */
static void emitMovImmToMemDisp(Emitter &e, int base, int32_t imm, int32_t disp)
{
    int mod;
    if (disp == 0 && (base & 7) != 5)       mod = 0;
    else if (disp >= -128 && disp <= 127)   mod = 1;
    else                                    mod = 2;
    e.raw({rexFor(true, 0, 0, base), 0xc7, (mod<<6) | (base&7)});
    if ((base & 7) == 4) e.raw({0x24});     // (%rsp)/(%r12) need a SIB byte
    if (mod == 1)      e.raw({(int)(uint8_t)(int8_t)disp});
    else if (mod == 2) e.imm32(disp);
    e.imm32(imm);
    e.tick();
}

// add/sub $imm,%gs:disp32   (REX.W 83 /ext ib, or REX.W 81 /ext id)
//   ext 0 = add, ext 5 = sub.  *** WRITES EFLAGS *** -- only emitted where the
//   spec (re-verified by rewrite.py) says they are dead.
static void emitAddImmToGs(Emitter &e, int ext, int32_t imm, int32_t off)
{
    e.raw({0x65, 0x48});
    if (imm >= -128 && imm <= 127)
    {
        e.raw({0x83, 0x04 | (ext << 3), 0x25});
        e.imm32(off);
        e.raw({(int)(uint8_t)(int8_t)imm});
    }
    else
    {
        e.raw({0x81, 0x04 | (ext << 3), 0x25});
        e.imm32(off);
        e.imm32(imm);
    }
    e.tick();
}

// movq $imm32,%gs:disp32   (REX.W C7 /0 id; the immediate is sign-extended).
//   No flags.  The sync countdown MUST be re-armed to an ABSOLUTE `sync', not
//   incremented by it -- see emitSyncFast().
static void emitMovImmToGs(Emitter &e, int32_t imm, int32_t off)
{
    e.raw({0x65, 0x48, 0xc7, 0x04, 0x25});
    e.imm32(off);
    e.imm32(imm);
    e.tick();
}

// add/sub %src,%gs:disp32   (REX.W 01|29 /r).  WRITES EFLAGS.
static void emitAddRegToGs(Emitter &e, bool sub, int src, int32_t off)
{
    e.raw({0x65, rexFor(true, src, 0, 0), (sub? 0x29: 0x01),
           0x04 | ((src & 7) << 3), 0x25});
    e.imm32(off);
    e.tick();
}

// add/sub $imm32,%r   (REX.W 81 /ext id).  WRITES EFLAGS.
static void emitAddImmReg(Emitter &e, bool sub, int32_t imm, int r)
{
    e.raw({rexFor(true, 0, 0, r), 0x81, 0xc0 | ((sub? 5: 0) << 3) | (r & 7)});
    e.imm32(imm);
    e.tick();
}

// mov $imm32,%ecx   (zero-extends to rcx; no flags)
static void emitMovEcxImm(Emitter &e, int32_t v)
{
    e.raw({0xb9});
    e.imm32(v);
    e.tick();
}

// movq %xmmN,%dst   (66 REX.W 0F 7E /r)
static void emitMovqXmmToGp(Emitter &e, int dst, int xmm)
{
    e.raw({0x66, rexFor(true, xmm, 0, dst), 0x0f, 0x7e,
           0xc0 | ((xmm&7)<<3) | (dst&7)});
    e.tick();
}

// pextrq $1,%xmmN,%dst   (66 REX.W 0F 3A 16 /r ib)  [SSE4.1]
static void emitPextrqHi(Emitter &e, int dst, int xmm)
{
    e.raw({0x66, rexFor(true, xmm, 0, dst), 0x0f, 0x3a, 0x16,
           0xc0 | ((xmm&7)<<3) | (dst&7), 0x01});
    e.tick();
}

// ptwrite %r   (F3 REX.W 0F AE /4)
static void emitPtwriteReg(Emitter &e, int r, int space)
{
    while (e.since_ptwrite < space)
        emitNop(e);
    e.pushLog(e.body_len);
    e.raw({0xf3, rexFor(true, 0, 0, r), 0x0f, 0xae, 0xe0 | (r&7)});
    e.since_ptwrite = 0;
    n_emitted++;
}

// jmp rel8 / jrcxz rel8
static void emitJmp8(Emitter &e, int8_t d)   { e.raw({0xeb, (int)(uint8_t)d}); e.tick(); }
static void emitJrcxz8(Emitter &e, int8_t d) { e.raw({0xe3, (int)(uint8_t)d}); e.tick(); }

/****************************************************************************/
/* Memory-operand re-encoding                                               */
/****************************************************************************/

/*
 * Map an e9tool Register to an x86 GP encoding number, or -1.
 */
static int gpNum(Register r)
{
    switch (r)
    {
        case REGISTER_RAX: return 0;  case REGISTER_RCX: return 1;
        case REGISTER_RDX: return 2;  case REGISTER_RBX: return 3;
        case REGISTER_RSP: return 4;  case REGISTER_RBP: return 5;
        case REGISTER_RSI: return 6;  case REGISTER_RDI: return 7;
        case REGISTER_R8:  return 8;  case REGISTER_R9:  return 9;
        case REGISTER_R10: return 10; case REGISTER_R11: return 11;
        case REGISTER_R12: return 12; case REGISTER_R13: return 13;
        case REGISTER_R14: return 14; case REGISTER_R15: return 15;
        default: return -1;
    }
}

struct Mem
{
    int      seg   = 0;         // 0 none, 0x64 fs, 0x65 gs
    int      base  = -1;        // GP encoding or -1
    int      index = -1;
    int      scale = 1;
    int64_t  disp  = 0;
    bool     rip   = false;
    intptr_t riptarget = 0;     // absolute target of a rip-relative operand
};

/*
 * Extract the (unique) memory operand of the instruction.  Returns false if
 * there is none, or if it uses a form we refuse to re-encode.
 */
static bool getMem(const InstrInfo *I, Mem &m, const char **why)
{
    const OpInfo *mem = nullptr;
    for (uint8_t i = 0; i < I->count.op; i++)
    {
        if (I->op[i].type == OPTYPE_MEM)
        {
            if (mem != nullptr) { *why = "more than one memory operand"; return false; }
            mem = &I->op[i];
        }
    }
    if (mem == nullptr) { *why = "no memory operand"; return false; }

    const MemOpInfo &mi = mem->mem;
    switch (mi.seg)
    {
        case REGISTER_NONE: m.seg = 0;    break;
        case REGISTER_FS:   m.seg = 0x64; break;
        case REGISTER_GS:   m.seg = 0x65; break;
        default:            m.seg = 0;    break;   // cs/ds/es/ss: flat, ignore
    }
    m.disp  = mi.disp;
    m.scale = (mi.scale == 0? 1: mi.scale);
    if (mi.base == REGISTER_RIP || mi.base == REGISTER_EIP)
    {
        m.rip = true;
        m.riptarget = I->address + (intptr_t)I->size + (intptr_t)mi.disp;
        if (mi.index != REGISTER_NONE) { *why = "rip-relative with index"; return false; }
        return true;
    }
    m.base  = (mi.base  == REGISTER_NONE? -1: gpNum(mi.base));
    m.index = (mi.index == REGISTER_NONE? -1: gpNum(mi.index));
    if (mi.base != REGISTER_NONE && m.base < 0)  { *why = "non-GP base register";  return false; }
    if (mi.index != REGISTER_NONE && m.index < 0){ *why = "non-GP index register"; return false; }
    if (m.index == 4)                            { *why = "%rsp as index";         return false; }
    return true;
}

/*
 * Emit `<opcode> <mem>,<reg>`-style ModRM/SIB/disp for memory operand `m` with
 * the given reg field.  `rsp_delta` is added to the displacement when %rsp is
 * the base (the trampoline may have moved %rsp).  Prefixes (segment, REX) are
 * emitted by the caller through `rex_extra`.
 */
static void emitModRM(Emitter &e, const Mem &m, int regfield, int32_t rsp_delta)
{
    if (m.rip)
    {
        e.raw({0x00 | ((regfield&7)<<3) | 0x05});    // mod=00, rm=101 (rip+disp32)
        e.rel32(m.riptarget);
        return;
    }
    int64_t disp = m.disp;
    if (m.base == 4 || m.index == 4)                 // cannot happen for index
        disp += rsp_delta;

    bool need_sib   = (m.index >= 0) || (m.base < 0) || ((m.base & 7) == 4);
    bool disp_is_8  = (disp >= -128 && disp <= 127);
    int  mod;
    if (m.base < 0)                 mod = 0;         // disp32 (no base)
    else if (disp == 0 && (m.base & 7) != 5) mod = 0;
    else if (disp_is_8)             mod = 1;
    else                            mod = 2;

    int rm = (need_sib? 4: (m.base & 7));
    e.raw({(mod<<6) | ((regfield&7)<<3) | rm});
    if (need_sib)
    {
        int ss = (m.scale == 8? 3: m.scale == 4? 2: m.scale == 2? 1: 0);
        int idx = (m.index < 0? 4: (m.index & 7));
        int bas = (m.base  < 0? 5: (m.base  & 7));
        e.raw({(ss<<6) | (idx<<3) | bas});
        if (m.base < 0) mod = 0;                     // base=101,mod=00 -> disp32
    }
    if (m.base < 0)
        e.imm32((int32_t)disp);
    else if (mod == 1)
        e.raw({(int)(uint8_t)(int8_t)disp});
    else if (mod == 2)
        e.imm32((int32_t)disp);
}

static int memRexBits(const Mem &m, int regfield)
{
    int rex = 0;
    if (regfield >= 8)              rex |= 0x4;
    if (m.index >= 8)               rex |= 0x2;
    if (!m.rip && m.base >= 8)      rex |= 0x1;
    return rex;
}

// ptwrite <mem>   (size 4 or 8)
static void emitPtwriteMem(Emitter &e, const Mem &m, int size, int32_t rsp_delta,
    int space)
{
    while (e.since_ptwrite < space)
        emitNop(e);
    e.pushLog(e.body_len);
    // Prefix order: the mandatory `F3' FIRST, then the segment override, then
    // REX -- which must be the last prefix before the opcode.  `65 F3 REX' is
    // architecturally the same instruction but binutils decodes it as three
    // separate prefixes and loses both REX.W and the segment.
    e.raw({0xf3});
    if (m.seg != 0) e.raw({m.seg});
    int rex = memRexBits(m, 4) | (size == 8? 0x8: 0x0);
    if (rex != 0) e.raw({0x40 | rex});
    e.raw({0x0f, 0xae});
    emitModRM(e, m, 4, rsp_delta);
    e.since_ptwrite = 0;
    n_emitted++;
}

// mov disp(%base),%dst  -- the mirror of emitMovToMemDisp (reading a borrowed
// scratch register's saved copy back off the stack).
static void emitMovFromMemDisp(Emitter &e, int dst, int base, int32_t disp)
{
    int mod;
    if (disp == 0 && (base & 7) != 5)       mod = 0;
    else if (disp >= -128 && disp <= 127)   mod = 1;
    else                                    mod = 2;
    e.raw({rexFor(true, dst, 0, base), 0x8b,
           (mod<<6) | ((dst&7)<<3) | (base&7)});
    if ((base & 7) == 4) e.raw({0x24});
    if (mod == 1)      e.raw({(int)(uint8_t)(int8_t)disp});
    else if (mod == 2) e.imm32(disp);
    e.tick();
}

static void emitLoadMem(Emitter &e, int dst, const Mem &m, int size,
    int32_t rsp_delta)
{
    if (m.seg != 0) e.raw({m.seg});
    switch (size)
    {
        case 8:
            e.raw({0x40 | 0x8 | memRexBits(m, dst)});
            e.raw({0x8b});
            break;
        case 4:                     // mov r/m32,r32 (zero-extends)
        {
            int rex = memRexBits(m, dst);
            if (rex != 0) e.raw({0x40 | rex});
            e.raw({0x8b});
            break;
        }
        case 2:                     // movzwq
            e.raw({0x40 | 0x8 | memRexBits(m, dst)});
            e.raw({0x0f, 0xb7});
            break;
        case 1:                     // movzbq
            e.raw({0x40 | 0x8 | memRexBits(m, dst)});
            e.raw({0x0f, 0xb6});
            break;
    }
    emitModRM(e, m, dst, rsp_delta);
    e.tick();
}


/****************************************************************************/
/* Ground-truth address logging                                             */
/****************************************************************************/

// lea <mem>,%dst   -- the EFFECTIVE ADDRESS of the operand, with NO segment
// prefix (`lea' ignores it anyway; a %fs: operand adds the segment base
// separately, see emitGt).
static void emitLeaMem(Emitter &e, int dst, const Mem &m, int32_t rsp_delta)
{
    e.raw({0x48 | memRexBits(m, dst)});
    e.raw({0x8d});
    emitModRM(e, m, dst, rsp_delta);
    e.tick();
}

// lea (%base,%index,1),%dst   (index is never %rsp: it comes from sAlloc)
static void emitLeaBaseIndex(Emitter &e, int dst, int base, int index)
{
    const int mod = ((base & 7) == 5? 1: 0);    // (%rbp)/(%r13): mod=01, disp8=0
    e.raw({rexFor(true, dst, index, base), 0x8d,
           (mod<<6) | ((dst&7)<<3) | 0x04,
           (0<<6) | ((index&7)<<3) | (base&7)});
    if (mod == 1) e.raw({0x00});
    e.tick();
}

// lea TARGET(%rip),%dst  -- materialises an ORIGINAL link-time address as the
// runtime address it has in THIS process, whatever the load base.
static void emitLeaRip(Emitter &e, int dst, intptr_t target)
{
    e.raw({rexFor(true, dst, 0, 0), 0x8d, 0x05 | ((dst&7)<<3)});
    e.rel32(target);
    e.tick();
}

// mov %src,%dst
static void emitMovRegReg(Emitter &e, int dst, int src)
{
    e.raw({rexFor(true, src, 0, dst), 0x89, 0xc0 | ((src&7)<<3) | (dst&7)});
    e.tick();
}

// Ordinary forms produce one address record. Completed REP STOS uses the
// independently recorded range protocol in ptgt_format.h and expands offline.
enum GtForm { GT_NONE = 0, GT_MEM, GT_PUSH, GT_POP, GT_RBP,
              GT_STOSB, GT_STOSW, GT_STOSD, GT_STOSQ };

static bool gtRep(GtForm form) { return form >= GT_STOSB && form <= GT_STOSQ; }

static bool gtBadMnemonic(Mnemonic mn)
{
    switch (mn)
    {
        // no memory access at all despite a memory OPERAND
        case MNEMONIC_LEA: case MNEMONIC_NOP:
        // a `rep'-able string instruction: one instruction, N accesses
        case MNEMONIC_STOSB: case MNEMONIC_STOSW: case MNEMONIC_STOSD:
        case MNEMONIC_STOSQ: case MNEMONIC_LODSB: case MNEMONIC_LODSW:
        case MNEMONIC_LODSD: case MNEMONIC_LODSQ: case MNEMONIC_SCASB:
        case MNEMONIC_SCASW: case MNEMONIC_SCASD: case MNEMONIC_SCASQ:
        case MNEMONIC_CMPSB: case MNEMONIC_CMPSW: case MNEMONIC_CMPSD:
        case MNEMONIC_CMPSQ: case MNEMONIC_MOVSB: case MNEMONIC_MOVSW:
        case MNEMONIC_MOVSQ: case MNEMONIC_INSB:  case MNEMONIC_INSW:
        case MNEMONIC_INSD:  case MNEMONIC_OUTSB: case MNEMONIC_OUTSW:
        case MNEMONIC_OUTSD: case MNEMONIC_XLAT:
        // whole-register-file save/restore: many accesses, and VEX cannot lift
        // them either (`ptrecon' emits no record at all)
        case MNEMONIC_XSAVE:  case MNEMONIC_XSAVE64: case MNEMONIC_XSAVEC:
        case MNEMONIC_XSAVEC64: case MNEMONIC_XSAVES: case MNEMONIC_XSAVES64:
        case MNEMONIC_XSAVEOPT: case MNEMONIC_XSAVEOPT64:
        case MNEMONIC_XRSTOR: case MNEMONIC_XRSTOR64: case MNEMONIC_XRSTORS:
        case MNEMONIC_XRSTORS64:
        case MNEMONIC_FXSAVE: case MNEMONIC_FXSAVE64:
        case MNEMONIC_FXRSTOR: case MNEMONIC_FXRSTOR64:
        case MNEMONIC_PUSHA: case MNEMONIC_PUSHAD:
        case MNEMONIC_POPA:  case MNEMONIC_POPAD:
        case MNEMONIC_ENTER:
        // prefetch/cache hints: a memory operand that is never a data access
        case MNEMONIC_PREFETCH: case MNEMONIC_PREFETCHNTA:
        case MNEMONIC_PREFETCHT0: case MNEMONIC_PREFETCHT1:
        case MNEMONIC_PREFETCHT2: case MNEMONIC_PREFETCHW:
        case MNEMONIC_CLFLUSH: case MNEMONIC_CLFLUSHOPT: case MNEMONIC_CLWB:
            return true;
        default:
            return false;
    }
}

static GtForm gtForm(const InstrInfo *I, Mem &m)
{
    if (!gt_skip.empty() && gt_skip.count(I->address) != 0)
        return GT_NONE;                 // rewrite.py --gt-skip / --gt-retry
    // Only REP STOS with 64-bit addresses, no segment overrides. SCAS/CMPS
    // have data-dependent stopping conditions; MOVS has two operands. They
    // remain explicitly unsupported until separately implemented and tested.
    bool rep = false, supported = true;
    for (unsigned j = 0; j < I->encoding.offset.opcode; ++j) {
        const uint8_t b = I->data[j];
        if (b == 0xf3) rep = true;
        else if (b != 0x66 && (b < 0x40 || b > 0x4f)) supported = false;
    }
    if (rep && supported) switch (I->mnemonic) {
        case MNEMONIC_STOSB: return GT_STOSB;
        case MNEMONIC_STOSW: return GT_STOSW;
        case MNEMONIC_STOSD: return GT_STOSD;
        case MNEMONIC_STOSQ: return GT_STOSQ;
        default: break;
    }
    if (gtBadMnemonic(I->mnemonic))
        return GT_NONE;
    int n_mem = 0;
    for (uint8_t i = 0; i < I->count.op; i++)
        if (I->op[i].type == OPTYPE_MEM)
            n_mem++;
    const bool stackish =
        (I->mnemonic == MNEMONIC_PUSH || I->mnemonic == MNEMONIC_PUSHF ||
         I->mnemonic == MNEMONIC_PUSHFD || I->mnemonic == MNEMONIC_PUSHFQ ||
         I->mnemonic == MNEMONIC_POP || I->mnemonic == MNEMONIC_POPF ||
         I->mnemonic == MNEMONIC_POPFD || I->mnemonic == MNEMONIC_POPFQ ||
         I->mnemonic == MNEMONIC_LEAVE ||
         (I->category & (CATEGORY_CALL | CATEGORY_RETURN)) != 0);
    if (n_mem == 1 && !stackish)
    {
        // A single explicit memory operand: `mov (%rax),%rdx', `add %rdx,(%rax)'
        // (one MT_RMW record), `movsd 8(%rsp),%xmm0', ...
        const char *why = "";
        if (!getMem(I, m, &why))
            return GT_NONE;
        if (m.seg == 0x65)              // %gs: `ptrecon' reports it unknown and
            return GT_NONE;             // the buffer sink owns %gs anyway
        return GT_MEM;
    }
    if (n_mem != 0)
        return GT_NONE;                 // `push (%rax)', `call *(%rax)': two
                                        // accesses, and their order matters
    if (!opt_gt_stack)
        return GT_NONE;
    switch (I->mnemonic)
    {
        case MNEMONIC_PUSH: case MNEMONIC_PUSHF: case MNEMONIC_PUSHFD:
        case MNEMONIC_PUSHFQ:
            return GT_PUSH;             // one store at %rsp-8
        case MNEMONIC_POP: case MNEMONIC_POPF: case MNEMONIC_POPFD:
        case MNEMONIC_POPFQ:
            return GT_POP;              // one load at %rsp
        case MNEMONIC_LEAVE:
            return GT_RBP;              // `mov %rbp,%rsp; pop %rbp': load at %rbp
        default: break;
    }
    if ((I->category & CATEGORY_CALL) != 0)
        return GT_PUSH;                 // the return address, at %rsp-8
    if ((I->category & CATEGORY_RETURN) != 0)
        return GT_POP;                  // the return address, at %rsp
    return GT_NONE;
}

#define RED_ZONE    0x80

/****************************************************************************/
/* Keyframe guard                                                           */
/****************************************************************************/

/* Hand out the next 8-byte keyframe countdown counter: the link-time address
 * of a cell in the image's reserved data region. */
static intptr_t newCounter()
{
    intptr_t c = opt_counterbase + 8 * n_counters;
    n_counters++;
    if (opt_ncounters >= 0 && n_counters > opt_ncounters)
        error("ptlog: internal: %ld keyframe counters emitted but only %ld "
            "were reserved (rewrite.py and the plugin disagree)",
            n_counters, opt_ncounters);
    return c;
}

static void emitPushfq(Emitter &e)         { e.raw({0x9c}); e.tick(); }
static void emitPopfq(Emitter &e)          { e.raw({0x9d}); e.tick(); }

/* --- keyframe primitives ------------------------------------------------ */

/* jcc rel8/rel32 over a FORWARD span of `len' bytes (cc = the low nibble of
 * the short opcode: 4 = je/jz, 5 = jne/jnz).  A resync site with 14 logged
 * registers, or any keyframe in the buffer sink, is far longer than a `rel8'
 * can reach, so the width is chosen from the span. */
static void emitJccFwd(Emitter &e, int cc, long len)
{
    if (len >= 0 && len <= 127)
        e.raw({0x70 | cc, (int)(uint8_t)(int8_t)len});
    else
    {
        e.raw({0x0f, 0x80 | cc});
        e.imm32((int32_t)len);
    }
    e.tick();
}

// decq CNT(%rip)   (REX.W FF /1, mod=00 rm=101)
static void emitDecRip(Emitter &e, intptr_t target)
{
    e.raw({0x48, 0xff, 0x0d});
    e.rel32(target);
    e.tick();
}

// movq $imm32,CNT(%rip)   (REX.W C7 /0, mod=00 rm=101, then imm32)
//
// E9Patch computes a {"rel32":T} field as `T - (address of the field) - 4',
// i.e. it assumes the field is the LAST four bytes of the instruction.  Here
// four immediate bytes follow it, so the target is biased by -4 to land on T.
static void emitMovImmToRip(Emitter &e, int32_t imm, intptr_t target)
{
    e.raw({0x48, 0xc7, 0x05});
    e.rel32(target - 4);
    e.imm32(imm);
    e.tick();
}

/* The two keyframe-countdown operations on the cell `newCounter' handed out.
 * Both write EFLAGS. */
static void emitDecCnt(Emitter &e, intptr_t cnt)          { emitDecRip(e, cnt); }
static void emitMovImmToCnt(Emitter &e, int32_t imm, intptr_t cnt)
{
    emitMovImmToRip(e, imm, cnt);
}

/* Append a sub-emitter's chunks (and its recorded offsets) at `e`s end. */
static void appendEmitter(Emitter &e, const Emitter &t)
{
    long base = e.body_len;
    for (const Chunk &c: t.chunks)
    {
        if (c.kind == CH_BYTES)      e.raw(c.bytes);
        else if (c.kind == CH_REL32) e.rel32(c.target);
        else error("ptlog: internal: cannot append a $instr chunk");
    }
    for (size_t i = 0; i < t.log_offs.size(); i++)
    {
        e.log_offs.push_back(base + t.log_offs[i]);
        e.log_cnt.push_back   (t.log_cnt[i]);
        e.log_kfper.push_back (t.log_kfper[i]);
        e.log_kfbr.push_back  (t.log_kfbr[i]   < 0? -1: base + t.log_kfbr[i]);
        e.log_kfjoin.push_back(t.log_kfjoin[i] < 0? -1: base + t.log_kfjoin[i]);
    }
    for (long o: t.sync_offs)
        e.sync_offs.push_back(base + o);
    for (size_t i = 0; i < t.tnt_offs.size(); i++)
    {
        e.tnt_offs.push_back(base + t.tnt_offs[i]);
        e.tnt_role.push_back(t.tnt_role[i]);
    }
    e.since_ptwrite = t.since_ptwrite;
}

/*
 * A `resync' site: the registers are logged UNCONDITIONALLY, but only every
 * K-th execution of the site.
 *
 *      dec  CNT(%rip)
 *      jnz  .Ljoin           <- one TNT bit: PT says whether the log ran
 *      mov  $K,CNT(%rip)
 *      <ptwrite / buffer store, one per register>
 *  .Ljoin:
 *
 * One counter per SITE, so all of its registers are logged in the same
 * execution and the reconstructor re-anchors the whole loop-carried set at
 * once.  The counter cell starts at 1, so the first execution is a keyframe.
 */
static void emitKeyframeGuarded(Emitter &e, long kf,
    const std::function<void(Emitter &)> &body)
{
    if (kf <= 1)
    {
        body(e);                        // K <= 1: log on every execution
        return;
    }
    const intptr_t cnt = newCounter();
    Emitter t;
    t.since_ptwrite = e.since_ptwrite + 2;      // dec + jnz
    emitMovImmToCnt(t, (int32_t)kf, cnt);
    body(t);
    emitDecCnt(e, cnt);
    long kf_br = e.body_len;
    emitJccFwd(e, 5, t.body_len);               // jnz .Ljoin
    size_t n0 = e.log_offs.size();
    appendEmitter(e, t);
    long kf_join = e.body_len;
    for (size_t i = n0; i < e.log_offs.size(); i++)
    {
        e.log_cnt[i]    = (long)cnt;
        e.log_kfper[i]  = kf;
        e.log_kfbr[i]   = kf_br;
        e.log_kfjoin[i] = kf_join;
    }
    n_kf_resync++;
}

/****************************************************************************/
/* Trampoline body generation                                               */
/****************************************************************************/

/*
 * LIVENESS-AWARE SCRATCH REGISTERS.
 *
 * A trampoline that needs a scratch register can push it and pop it again,
 * and because a `push' writes below the original %rsp it has to step over the
 * 128-byte System V red zone first:
 *
 *      lea -0x80(%rsp),%rsp ; push .. ; <body> ; pop .. ; lea 0x80(%rsp),%rsp
 *
 * The analyzer publishes, per site, up to six 64-bit GP registers that are
 * DEAD there (`dead_regs'), and `rewrite.py' re-verifies them against the
 * image's own bytes before passing the survivors in the site file's
 * `:S<mask>' op field.  A trampoline takes its scratch from that set instead,
 * and then neither pushes nor pops -- and if nothing is pushed the red-zone
 * step is not needed either.
 *
 * `Scratch' is the per-site allocator.  It hands out dead registers first and
 * falls back to push/pop (emitting the red-zone `lea' lazily, at most once)
 * when the site has none left.  `avoid' holds the registers the trampoline
 * must not clobber: every register the site LOGS and the base/index of its
 * memory operand.
 */
#define SCR_CNT     1       /* %rcx -- the flags-live countdown needs `jrcxz' */

/* The address currently being patched -- diagnostics only. */
static intptr_t g_patch_addr = 0;

struct Scratch
{
    Emitter *e;
    unsigned avail;         // dead GP registers still free
    unsigned avoid;         // registers this site must not clobber
    // `avoid' is two different things.  A register that only appears as a
    // base/index of the site's memory operand
    // is HARD: the trampoline reads that operand, so the register has to hold
    // its program value at that point and no push can give it back in time.
    // A register the site LOGS is SOFT: `push' saves the program's copy on the
    // stack, and the logging code can take the value from that slot instead of
    // from the register, so the register itself is usable as scratch.
    unsigned hard;          // never scratch (%rsp, memory base/index)
    unsigned soft;          // logged registers: scratch, value read off the stack
    unsigned borrowed;      // ... the ones actually taken that way
    int      pushed[8];
    int      pdelta[8];     // S.delta immediately after each push
    int      npushed;
    bool     moved;         // the red-zone `lea' has been emitted
    int      delta;         // how far the prologue has moved %rsp
    int      sync_cnt;      // the flags-live countdown's %rcx  (-1 = not yet)
    int      sync_scr;      // ... and the marker's own scratch
};

static void sInit(Scratch &S, Emitter &e, unsigned dead, unsigned avoid,
    unsigned hard = ~0u)
{
    S.e        = &e;
    S.avoid    = avoid | (1u << 4);             // %rsp is never scratch
    // The default (`hard' = all) is the old, conservative behaviour: nothing is
    // borrowable unless the caller says which registers are only soft.
    S.hard     = (hard | (1u << 4)) & S.avoid;
    S.soft     = S.avoid & ~S.hard;
    S.borrowed = 0;
    S.avail    = (opt_liveness? dead: 0u) & ~S.avoid;
    S.npushed  = 0;
    S.moved    = false;
    S.delta    = 0;
    S.sync_cnt = -1;
    S.sync_scr = -1;
}

/* Where a borrowed register's program value sits, relative to the CURRENT
 * %rsp; -1 when the register still holds it itself. */
static int sSavedOff(const Scratch &S, int reg)
{
    if (!(S.borrowed & (1u << reg)))
        return -1;
    for (int i = 0; i < S.npushed; i++)
        if (S.pushed[i] == reg)
            return S.delta - S.pdelta[i];
    return -1;
}

/* Reserve stack space (the red-zone step) before the first push. */
static void sOpenStack(Scratch &S)
{
    if (S.moved)
        return;
    emitLeaRsp(*S.e, -RED_ZONE);
    S.moved = true;
    S.delta = RED_ZONE;
}

/* Any register that is free at this site, else one pushed onto the stack. */
static int sAlloc(Scratch &S, unsigned extra = 0)
{
    unsigned m = S.avail & ~extra;
    if (m != 0)
    {
        int r = __builtin_ctz(m);
        S.avail &= ~(1u << r);
        n_scr_dead++;
        return r;
    }
    sOpenStack(S);
    unsigned used = S.avoid | extra;
    for (int i = 0; i < S.npushed; i++)
        used |= 1u << S.pushed[i];
    // ORDER MATTERS, and not for correctness: a pushed register is restored, so
    // any choice is correct, but the push/pop pair puts the trampoline on the
    // dependency chain of whatever the program keeps in that register.  %r10 and
    // %r11 are the System V *scratch* registers -- caller-saved, not used for
    // arguments, and the ones a compiler reaches for last -- so they are the
    // least likely to hold a loop-carried value (preferring %rax/%rcx/%rdx
    // measurably slows a loop whose accumulator lives in one of them).
    static const int order[] = {10, 11, 8, 9, 0, 1, 2, 6, 7, 3, 5, 12, 13, 14, 15};
    int r = -1;
    for (int x: order)
        if ((used & (1u << x)) == 0) { r = x; break; }
    bool borrow = false;
    if (r < 0)
    {
        // Nothing outside the site's own set is left.  Borrow one of the
        // registers the site LOGS -- its program value is on the stack from
        // the `push' below (every push this trampoline makes happens before any
        // value is read), and the logging code takes it from there.  This is
        // the only way a site that logs 15 GP registers and indexes its memory
        // operand with the 16th can have a buffer-sink cursor at all.
        unsigned used2 = (used & ~S.soft) | S.hard | extra;
        for (int i = 0; i < S.npushed; i++)
            used2 |= 1u << S.pushed[i];
        for (int x: order)
            if ((used2 & (1u << x)) == 0) { r = x; borrow = true; break; }
    }
    if (r < 0)
        error("ptlog: internal: no scratch register left at 0x%" PRIxPTR
            " (used=%#x, avoid=%#x, hard=%#x, avail=%#x, pushed=%d)",
            g_patch_addr, used, S.avoid, S.hard, S.avail, S.npushed);
    emitPush(*S.e, r);
    S.pushed[S.npushed] = r;
    S.delta += 8;
    S.pdelta[S.npushed++] = S.delta;
    if (borrow) { S.borrowed |= 1u << r; n_scr_borrow++; }
    n_scr_push++;
    return r;
}

/* The register that holds `reg''s PROGRAM value here: `reg' itself, or `tmp'
 * loaded from the stack slot the borrow left it in. */
static int sProgReg(Emitter &e, Scratch &S, int reg, int tmp)
{
    int off = sSavedOff(S, reg);
    if (off < 0)
        return reg;
    emitMovFromMemDisp(e, tmp, 4, (int32_t)off);
    return tmp;
}

/* One PARTICULAR register (the sync countdown's `jrcxz' can only test %rcx). */
static int sAllocFixed(Scratch &S, int want)
{
    if (S.avail & (1u << want))
    {
        S.avail &= ~(1u << want);
        n_scr_dead++;
        return want;
    }
    for (int i = 0; i < S.npushed; i++)
        if (S.pushed[i] == want)
            error("ptlog: internal: scratch %%%s allocated twice", GP_NAMES[want]);
    sOpenStack(S);
    emitPush(*S.e, want);
    S.pushed[S.npushed] = want;
    S.delta += 8;
    S.pdelta[S.npushed++] = S.delta;
    if (S.soft & (1u << want)) { S.borrowed |= 1u << want; n_scr_borrow++; }
    n_scr_push++;
    return want;
}

static void sFini(Scratch &S)
{
    for (int i = S.npushed - 1; i >= 0; i--)
        emitPop(*S.e, S.pushed[i]);
    if (S.moved)
        emitLeaRsp(*S.e, RED_ZONE);
}

/* The registers this run of ops was told are dead (their intersection). */
static unsigned runDead(const std::vector<const Op *> &ops, size_t oi, size_t oj)
{
    unsigned d = ~0u;
    for (size_t i = oi; i < oj; i++)
        d &= ops[i]->dregs;
    return d;
}

/*
 * The ground-truth logging sequence.  Flag-free by construction (only `mov'
 * and `lea'), so it can be planted at ANY instruction without knowing whether
 * EFLAGS is live:
 *
 *      mov  %gs:GTOFF,%cur          ; the per-thread cursor
 *      lea  <ea>,%scr               ; the ORIGINAL effective address
 *      mov  %scr,(%cur)
 *      lea  ORIG(%rip),%scr         ; the ORIGINAL instruction address
 *      mov  %scr,8(%cur)
 *      lea  16(%cur),%cur
 *      mov  %cur,%gs:GTOFF
 *
 * The store to `(%cur)' is the one that faults on the window's guard page, so
 * the runtime's SIGSEGV handler recognises it exactly as it does the buffer
 * sink's value store (`ptlog_store_base').
 */
// mov sign-extended imm32,%reg, without touching flags.
static void emitGtImmediate(Emitter &e, int reg, int32_t value)
{
    e.raw({rexFor(true, 0, 0, reg), 0xc7, 0xc0 | (reg & 7)});
    e.imm32(value); e.tick();
}

static void emitGtPair(Emitter &e, int cur, int value, int ip)
{
    // Publish per 16-byte slot: the existing guard handler can rotate at the
    // first store of ANY slot, including in the middle of a range descriptor.
    emitMovToMemDisp(e, cur, value, 0);
    emitMovToMemDisp(e, cur, ip, 8);
    emitLeaReg(e, cur, 16);
    emitMovToGs(e, cur, opt_gtoff);
}

static void emitGtRepBegin(Emitter &e, GtForm form, intptr_t orig)
{
    Scratch S;
    sInit(S, e, 0u, (1u << 1) | (1u << 7));  // preserve rcx/rdi inputs
    const int cur = sAlloc(S), tmp = sAlloc(S), key = sAlloc(S);
    emitMovFromGs(e, cur, opt_gtoff);
    emitGtImmediate(e, key, PTGT_REP_BASE);
    e.pushLog(e.body_len);
    emitGtPair(e, cur, 7, key);
    emitGtImmediate(e, key, PTGT_REP_COUNT);
    emitGtPair(e, cur, 1, key);
    emitPushfq(e); emitPop(e, tmp);  // snapshot DF; leave all flags unchanged
    emitGtImmediate(e, key, PTGT_REP_FLAGS);
    emitGtPair(e, cur, tmp, key);
    emitGtImmediate(e, tmp, 1 << (form - GT_STOSB));
    emitLeaRip(e, key, orig);
    emitGtPair(e, cur, tmp, key);
    sFini(S);
    ++n_gt_sites; ++n_gt_mem;
}

static void emitGtRepCommit(Emitter &e)
{
    Scratch S;
    sInit(S, e, 0u, 1u << 1);  // record the actual post-instruction rcx
    const int cur = sAlloc(S), key = sAlloc(S);
    emitMovFromGs(e, cur, opt_gtoff);
    emitGtImmediate(e, key, PTGT_REP_COMMIT);
    emitGtPair(e, cur, 1, key);
    sFini(S);
}

static void emitGt(Emitter &e, GtForm form, const Mem &m, intptr_t orig,
    unsigned site_avoid)
{
    if (gtRep(form)) { emitGtRepBegin(e, form, orig); return; }
    Scratch S;
    sInit(S, e, 0u, site_avoid);        // no dead-register information here
    const int cur = sAlloc(S);
    const int scr = sAlloc(S, 1u << cur);
    const bool fs = (form == GT_MEM && m.seg == 0x64);
    const int seg = (fs? sAlloc(S, (1u << cur) | (1u << scr)): -1);
    emitMovFromGs(e, cur, opt_gtoff);
    switch (form)
    {
        case GT_MEM:
            if (fs)
            {
                emitMovFromSeg(e, 0x64, seg, 0);    // TCB self-pointer = FS base
                emitLeaMem(e, scr, m, S.delta);
                emitLeaBaseIndex(e, scr, scr, seg);
            }
            else
                emitLeaMem(e, scr, m, S.delta);
            break;
        case GT_PUSH: emitLeaFromRsp(e, scr, S.delta - 8); break;
        case GT_POP:  emitLeaFromRsp(e, scr, S.delta);     break;
        case GT_RBP:  emitMovRegReg(e, scr, 5);            break;
        default: break;
    }
    e.pushLog(e.body_len);
    emitMovToMemDisp(e, cur, scr, 0);
    emitLeaRip(e, scr, orig);
    emitMovToMemDisp(e, cur, scr, 8);
    emitLeaReg(e, cur, 16);
    emitMovToGs(e, cur, opt_gtoff);
    sFini(S);
    n_gt_sites++;
    if (form == GT_MEM) n_gt_mem++; else n_gt_stack++;
}

/* ---- PTWRITE sink ------------------------------------------------------ */

static void emitPtwOpReg(Emitter &e, Scratch &S, int reg)
{
    if (reg == 4 && S.delta != 0)
    {
        // An earlier op in this run pushed, so %rsp is no longer the program's:
        // materialise the original one.  (With nothing pushed -- the usual case
        // -- `ptwrite %rsp' is exact and this does not happen.)
        int scr = sAlloc(S);
        emitLeaFromRsp(e, scr, S.delta);
        emitPtwriteReg(e, scr, opt_space);
        return;
    }
    if (reg < 16)
    {
        if (S.borrowed & (1u << reg))
        {
            // An earlier op of this run borrowed the register this one logs;
            // its program value is in the stack slot that borrow made.
            const int scr = sAlloc(S);
            emitPtwriteReg(e, sProgReg(e, S, reg, scr), opt_space);
            return;
        }
        emitPtwriteReg(e, reg, opt_space);
        return;
    }
    if (reg == REG_FS_BASE)
    {
        // glibc/x86-64 keeps the TCB self-pointer at %fs:0, so `mov %fs:0,%scr'
        // reads the thread's FS base without a syscall and without needing
        // CR4.FSGSBASE (`rdfsbase').
        int scr = sAlloc(S);
        emitMovFromSeg(e, 0x64, scr, 0);
        emitPtwriteReg(e, scr, opt_space);
        return;
    }
    // xmm: low qword then high qword, through a GP scratch.
    int xmm = reg - 16;
    int scr = sAlloc(S);
    emitMovqXmmToGp(e, scr, xmm);
    emitPtwriteReg(e, scr, opt_space);
    emitPextrqHi(e, scr, xmm);
    emitPtwriteReg(e, scr, opt_space);
}

static void emitPtwOpMem(Emitter &e, Scratch &S, const Mem &m, int size)
{
    // A 4- or 8-byte operand is logged in place with `ptwrite m32/m64' (the
    // site map's `payload_bits' records the 32-bit case); a 1- or 2-byte one
    // is zero-extended through a scratch register.
    if (size == 4 || size == 8)
    {
        emitPtwriteMem(e, m, size, S.delta, opt_space);
        return;
    }
    int scr = sAlloc(S);
    emitLoadMem(e, scr, m, size, S.delta);
    emitPtwriteReg(e, scr, opt_space);
}

/* ---- buffer sink ------------------------------------------------------- */

/*
 * Per-value store into the per-thread buffer addressed through %gs.  With a
 * dead register for the cursor and dead EFLAGS the whole site is
 *
 *   mov  %gs:CURSOR,%cur       #  9 bytes
 *   mov  %val,(%cur)           #  3      <- faults on the guard page when full
 *   add  $8,%gs:CURSOR         # 10
 *   [ add  $1,%gs:TOTAL        #          the sync countdown, `sync' > 0 only
 *     sub  $1,%gs:COUNT
 *     jg   .Lcont
 *     add  $SYNC,%gs:COUNT
 *     ptwrite %gs:TOTAL
 *    .Lcont: ]
 *
 * -- three instructions and 22 bytes for the value itself, against the 17
 * instructions and 117 bytes of the push/pop trampoline:
 *
 *   lea -0x80(%rsp),%rsp ; push %r11 ; push %r10 ; push %rcx
 *   mov %val,%r10 ; mov %gs:CURSOR,%r11 ; mov %r10,(%r11)
 *   lea 8(%r11),%r11 ; mov %r11,%gs:CURSOR
 *   mov %gs:COUNT,%rcx ; lea -1(%rcx),%rcx ; jrcxz .Lmark ; jmp .Lcont
 *   .Lmark: <5 instructions> .Lcont: mov %rcx,%gs:COUNT
 *   pop %rcx ; pop %r10 ; pop %r11 ; lea 0x80(%rsp),%rsp
 *
 * which is still what a site with LIVE EFLAGS gets, because `add'/`sub'/`dec'
 * write EFLAGS where `lea', `mov' and `jrcxz' do not.
 *
 * N values at one site share ONE cursor load and ONE cursor update; the stores
 * go to 0(%cur), 8(%cur) ... 8*(N-1)(%cur).  The marker payload stays EXACT:
 * it is `%gs:TOTAL', incremented by N at every site, so it is the true number
 * of values this thread has written.  `%gs:COUNT' only decides how OFTEN a
 * marker is emitted and may overshoot zero by up to N-1.
 *
 * Invariant relied on by the SIGSEGV handler in rt/ptlogrt.c: the faulting
 * store is a `mov %val,disp(%cur)', which the handler decodes to recover both
 * the cursor register and the displacement.
 */

/*
 * The two registers the flags-live countdown needs, allocated once per site.
 *
 * `spare' is a register the CALLER has finished with -- the buffer sink's
 * cursor, which is dead the moment the cursor update has been emitted.  Reusing
 * it for the marker's scratch saves a FOURTH pushed register on a site that has
 * neither a dead register nor dead flags.  `spare' is never %rsp and never a
 * register the site logs,
 * because it came out of `sAlloc' itself; the only register it must not
 * collide with is the countdown's own %rcx.
 */
static void sSyncRegs(Scratch &S, int spare = -1)
{
    if (S.sync_cnt < 0)
    {
        S.sync_cnt = sAllocFixed(S, SCR_CNT);
        if (spare >= 0 && spare != S.sync_cnt)
            S.sync_scr = spare;                 // free: the caller is done with it
        else
            S.sync_scr = sAlloc(S, 1u << S.sync_cnt);
    }
}

/*
 * The flags-live sync countdown: the original, flag-free block.
 *
 * `ahead': a run of N values emits its N countdowns AFTER all N stores (the
 * pushes and the cursor update are batched --
 * see emitBufRun), so when the j-th countdown fires the marker, N values are
 * already in the buffer but `%gs:TOTAL' has only counted j of them.  The
 * reconstructor's cv cursor is POSITIONAL and has consumed all N, so it would
 * realign backwards by N-j here and forwards by N-j at the next marker.  The
 * distance is a compile-time constant, so the marker simply PTWRITEs
 * `TOTAL + SYNC + ahead' -- one extra `lea' inside a block that runs 1-in-`sync'
 * times, and nothing at all when `ahead' is 0.  `%gs:TOTAL' itself keeps
 * counting countdowns, so the next marker is unaffected.
 */
/*
 * `synccarrier tnt': the marker's count travels in
 * TNT bits instead of a PTWRITE payload, for PT-capable CPUs without PTWRITE.
 * `%r' holds the payload the PTWRITE carrier would have written; the loop
 * destroys it (every caller's `%r' is dead after its marker) and clobbers
 * EFLAGS (every caller has them dead or saved here):
 *
 *   top: shr  $1,%r        # CF = the next bit (LSB first), ZF = no bits left
 *        jc   1f           # TNT: the bit                    ('b' in the map)
 *        nop               # runs iff the bit is 0           ('z')
 *     1: jnz  top          # TNT: more bits
 *
 * 8 bytes, 2 TNT bits per payload bit, bitlength(payload) iterations (at least
 * one), once per `sync' values.  The reconstructor collects the bits between
 * the marker's first instruction ('s' = offset 0 of the marker body) and its
 * END -- the instruction right after the loop, which the map lists as the
 * marker itself (`sync_offs') -- and realigns exactly as with a PTW payload.
 */
static void emitTntCount(Emitter &mark, int r)
{
    mark.tnt_offs.push_back(0); mark.tnt_role.push_back('s');
    const long top = mark.body_len;
    mark.raw({0x48 | (r >> 3), 0xd1, 0xe8 | (r & 7)});     // shr $1,%r
    mark.tnt_offs.push_back(mark.body_len); mark.tnt_role.push_back('b');
    mark.raw({0x72, 0x01});                                 // jc 1f
    mark.tnt_offs.push_back(mark.body_len); mark.tnt_role.push_back('z');
    mark.raw({0x90});                                       // nop
    mark.raw({0x75, (int)(uint8_t)(int8_t)(top - (mark.body_len + 2))});  // jnz top
    mark.tick(4);
    mark.sync_offs.push_back(mark.body_len);                // END: the caller's next instruction
}

// A marker body's sync/TNT offsets, rebased into the site body.
static void adoptMarker(Emitter &e, const Emitter &mark, long markbase)
{
    for (long o: mark.log_offs)
        e.sync_offs.push_back(markbase + o);    // the PTWRITE carrier's `ptwrite'
    for (long o: mark.sync_offs)
        e.sync_offs.push_back(markbase + o);    // the TNT carrier's END
    for (size_t i = 0; i < mark.tnt_offs.size(); i++)
    {
        e.tnt_offs.push_back(markbase + mark.tnt_offs[i]);
        e.tnt_role.push_back(mark.tnt_role[i]);
    }
}

static void emitSyncSlow(Emitter &e, Scratch &S, int spare = -1, long ahead = 0)
{
    sSyncRegs(S, spare);
    const int cnt = S.sync_cnt, scr = S.sync_scr;
    emitMovFromGs(e, cnt, GS_COUNT);
    emitLeaReg(e, cnt, -1);
    // jrcxz .Lmark ; jmp .Lcont ; .Lmark: <mark> ; .Lcont:
    Emitter mark;
    mark.since_ptwrite = 1000;          // the marker is isolated by construction
    emitMovFromGs(mark, scr, GS_TOTAL);
    emitLeaReg32(mark, scr, (int32_t)opt_sync);
    emitMovToGs(mark, scr, GS_TOTAL);
    if (ahead != 0)
        emitLeaReg32(mark, scr, (int32_t)ahead);
    if (opt_sync_tnt)
    {
        // EFLAGS are live here: save them below the red zone, 1-in-`sync'.
        emitLeaRsp(mark, -RED_ZONE);
        emitPushfq(mark);
        emitTntCount(mark, scr);
        emitPopfq(mark);                // END
        emitLeaRsp(mark, RED_ZONE);
    }
    else
        emitPtwriteReg(mark, scr, 0);
    emitMovEcxImm(mark, (int32_t)opt_sync);
    long marklen = mark.body_len;
    emitJrcxz8(e, 2);                   // skip the `jmp' below
    emitJmp8(e, (int8_t)marklen);
    long markbase = e.body_len;
    adoptMarker(e, mark, markbase);     // the marker's `ptwrite' (or TNT loop)
    for (const Chunk &c: mark.chunks)
        e.raw(c.bytes);                 // marker code is pure bytes
    emitMovToGs(e, cnt, GS_COUNT);
    e.since_ptwrite = 1000;             // the marker PTWRITE is 1-in-`sync'
}

/*
 * The flags-dead sync countdown: TWO instructions and no registers on the fast
 * path.  `%gs:COUNT' is the only per-value memory update -- the running total
 * `%gs:TOTAL', which is what the marker PTWRITEs and what the reconstructor
 * realigns its cv cursor to, is brought up to date INSIDE the marker, once per
 * `sync' values:
 *
 *      sub  $N,%gs:COUNT
 *      jg   .Lcont
 *      mov  %gs:COUNT,%r        # <= 0: how far the countdown overshot
 *      sub  $SYNC,%r            # r = COUNT - SYNC = -(values since the marker)
 *      sub  %r,%gs:TOTAL        # ... so TOTAL += values since the marker
 *      movq $SYNC,%gs:COUNT     # re-arm  (ABSOLUTE -- see below)
 *      ptwrite %gs:TOTAL
 *  .Lcont:
 *
 * The payload stays EXACT for any N (a site logging several values decrements
 * by N and may overshoot zero; the marker adds back exactly the overshoot),
 * which is what `Recon::define_from_ptw' PTW_SYNC needs.
 *
 * THE RE-ARM MUST BE ABSOLUTE.  `TOTAL += SYNC - COUNT' is only "values since
 * the last marker" if the countdown STARTED this interval at exactly SYNC.  An
 * `add $SYNC,%gs:COUNT' re-arm would leave COUNT at SYNC - overshoot, so the
 * NEXT marker would over-count by exactly the PREVIOUS marker's overshoot --
 * and the error accumulates, because TOTAL is a running total, until the
 * reconstructor's positional cv cursor is realigned to the wrong site at every
 * marker.  `movq $imm32,%gs:disp32' costs nothing over `addq' (same length)
 * and writes no flags.  Keeping TOTAL out of the fast path matters: each `%gs'
 * read-modify-write is a loop-carried store-to-load-forwarding chain of ~5
 * cycles, and a tight logging loop is latency-bound on them, not
 * throughput-bound on instructions.
 */
static void emitSyncFast(Emitter &e, Scratch &S, long nval, int spare = -1)
{
    emitAddImmToGs(e, 5, (int32_t)nval, GS_COUNT);       // sub $N,%gs:COUNT
    Emitter mark;
    mark.since_ptwrite = 1000;
    // A register for the marker only.  The caller's `spare' (the cursor, dead
    // once the cursor update is emitted) first, then anything the site has
    // free; otherwise pushed and popped INSIDE the marker, which runs
    // 1-in-`sync` times and therefore costs nothing measurable.
    int r;
    bool local_save = (spare < 0 && S.avail == 0);
    if (spare >= 0)
        r = spare;
    else if (!local_save)
    {
        r = __builtin_ctz(S.avail);
        S.avail &= ~(1u << r);
        n_scr_dead++;
    }
    else
    {
        // Nothing free: pick a register that is not one this site must keep,
        // and save it INSIDE the marker.
        static const int order[] = {10, 11, 8, 9, 0, 1, 2, 6, 7, 3, 5,
                                    12, 13, 14, 15};
        unsigned used = S.avoid;
        for (int i = 0; i < S.npushed; i++)
            used |= 1u << S.pushed[i];
        r = -1;
        for (int x: order)
            if ((used & (1u << x)) == 0) { r = x; break; }
        if (r < 0) r = 0;                   // every register is already saved
        emitLeaRsp(mark, -RED_ZONE);
        emitPush(mark, r);
    }
    emitMovFromGs(mark, r, GS_COUNT);
    emitAddImmReg(mark, /*sub=*/true, (int32_t)opt_sync, r);
    emitAddRegToGs(mark, /*sub=*/true, r, GS_TOTAL);
    // ABSOLUTE re-arm (see above).
    if (opt_sync_tnt)
    {
        emitMovFromGs(mark, r, GS_TOTAL);
        emitTntCount(mark, r);
        emitMovImmToGs(mark, (int32_t)opt_sync, GS_COUNT);     // END
    }
    else
    {
    emitMovImmToGs(mark, (int32_t)opt_sync, GS_COUNT);
    Mem tot;
    tot.seg = 0x65; tot.base = -1; tot.index = -1; tot.disp = GS_TOTAL;
    emitPtwriteMem(mark, tot, 8, 0, 0);
    }
    if (local_save)
    {
        emitPop(mark, r);
        emitLeaRsp(mark, RED_ZONE);
    }
    long marklen = mark.body_len;
    emitJccFwd(e, 0xf, marklen);        // jg .Lcont   (cc 0xf = jnle)
    long markbase = e.body_len;
    adoptMarker(e, mark, markbase);
    for (const Chunk &c: mark.chunks)
        e.raw(c.bytes);
    e.since_ptwrite = 1000;
}

static void emitSync(Emitter &e, Scratch &S, bool flags_dead, long nval,
    int spare = -1)
{
    if (opt_sync <= 0)
        return;
    if (flags_dead)
        emitSyncFast(e, S, nval, spare);
    else
        // The i-th countdown belongs to value i+1 of the run, but all `nval'
        // stores have already run -- tell its marker how far ahead the buffer
        // is.
        for (long i = 0; i < nval; i++)
            emitSyncSlow(e, S, spare, nval - 1 - i);
}

/*
 * How many 8-byte buffer slots one value occupies.  THE SINGLE DEFINITION: the
 * store loop of emitBufRun() follows it, and the plugin's data-area sizing
 * counts by it.  An xmm register is two slots (movq + pextrq); everything else
 * is one --
 * including `fs_base', whose encoding (REG_FS_BASE == 32) is above the xmm
 * range but which is stored as a single quadword.
 */
static long bufSlots(const Op &op)
{
    if (op.kind == 'm' || op.kind == 'i')
        return 1;
    return (op.reg >= 16 && op.reg != REG_FS_BASE)? 2: 1;
}

/*
 * Emit the values of `ops[oi..oj)` into the buffer as ONE block: one cursor
 * load, N stores, one cursor update, one countdown.
 */
static void emitBufRun(Emitter &e, Scratch &S, const std::vector<const Op *> &ops,
    size_t oi, size_t oj, const Mem &mem, bool flags_dead)
{
    bool need_val = false, has_rsp = false;
    for (size_t i = oi; i < oj; i++)
    {
        const Op &op = *ops[i];
        if (op.kind == 'm' || op.reg == REG_FS_BASE || op.reg >= 16)
            need_val = true;
        else if (op.reg == 4)
            has_rsp = true;
    }
    const int cur = sAlloc(S);                  // the cursor: always needed
    // Allocate the flags-live sync countdown's registers NOW, before the body,
    // so that every `push' this trampoline does happens up front.  Emitting
    // `push %rcx' in the middle -- between the cursor store-back and the
    // countdown's load of %gs:COUNT -- would put a stack store between two %gs
    // read-modify-writes, which measurably slows the flags-live path.  The
    // cursor is dead by the time the marker runs, so it doubles as the
    // marker's scratch and no second register has to be saved.
    if (opt_sync > 0 && !flags_dead)
        sSyncRegs(S, cur);
    if (has_rsp && S.delta != 0)
        need_val = true;                        // `%rsp' has to be un-moved first
    // A register this run LOGS that the cursor (or the countdown) had to
    // borrow is read back out of its stack slot, and that needs a temporary.
    for (size_t i = oi; i < oj; i++)
    {
        const Op &op = *ops[i];
        if (op.kind == 'r' && op.reg < 16 && (S.borrowed & (1u << op.reg)))
            need_val = true;
    }
    const int val = (need_val? sAlloc(S, 1u << cur): -1);

    emitMovFromGs(e, cur, GS_CURSOR);
    /* The stores go to 0(%cur), 8(%cur), ...: one slot per value, two for an
     * xmm register (a movq/pextrq pair), exactly as `bufSlots' counts. */
    long k = 0;
    auto store = [&](int src)
    {
        e.pushLog(e.body_len);
        emitMovToMemDisp(e, cur, src, (int32_t)(8 * k));
        n_emitted++;
        k++;
    };
    /* An `i' op stores its constant with no source register (see
     * emitMovImmToMemDisp): same slot accounting, one fewer scratch. */
    auto storeImm = [&](int32_t imm)
    {
        e.pushLog(e.body_len);
        emitMovImmToMemDisp(e, cur, imm, (int32_t)(8 * k));
        n_emitted++;
        k++;
    };
    for (size_t i = oi; i < oj; i++)
    {
        const Op &op = *ops[i];
        if (op.kind == 'i')
        {
            storeImm((int32_t)op.imm);
        }
        else if (op.kind == 'm')
        {
            emitLoadMem(e, val, mem, op.size, S.delta);
            store(val);
        }
        else if (op.reg == REG_FS_BASE)
        {
            emitMovFromSeg(e, 0x64, val, 0);    // TCB self-pointer == FS base
            store(val);
        }
        else if (op.reg >= 16)
        {
            emitMovqXmmToGp(e, val, op.reg - 16);
            store(val);
            emitPextrqHi(e, val, op.reg - 16);
            store(val);
        }
        else if (op.reg == 4 && S.delta != 0)
        {
            emitLeaFromRsp(e, val, S.delta);    // the ORIGINAL %rsp
            store(val);
        }
        else
            // Straight out of the program -- unless the cursor (or the sync
            // countdown) had to borrow this very register, in which case the
            // program's copy is in the stack slot the borrow's `push' made.
            store(sProgReg(e, S, op.reg, val));
    }
    if (flags_dead)
        emitAddImmToGs(e, 0, (int32_t)(8 * k), GS_CURSOR);
    else
    {
        emitLeaReg32(e, cur, (int32_t)(8 * k));
        emitMovToGs(e, cur, GS_CURSOR);
    }
    // The cursor is DEAD from here on: hand it to the marker as its scratch.
    emitSync(e, S, flags_dead, k, cur);
}

/****************************************************************************/
/* E9Tool plugin entry points                                               */
/****************************************************************************/

/* How many bytes of the trampoline body the "P"/"Q" layout rows carry.  Long
 * enough to be unique against anything E9Patch's own relocation can emit
 * (the longest of those is 8 bytes), short enough that the CSV stays small. */
#define PATTERN_MAX     24

#define OPTION_SITES    1
#define OPTION_SITEMAP  2
#define OPTION_RT       3
#define OPTION_LDFIX    4
#define OPTION_LDFIXBASE 5
#define OPTION_RTBASE   6

extern "C" void *e9_plugin_init(const Context *cxt)
{
    if (API_VERSION != cxt->api)
        error("bad API version; expected %u, found %u", API_VERSION, cxt->api);

    static const struct option long_options[] =
    {
        {"sites",   required_argument, nullptr, OPTION_SITES},
        {"sitemap", required_argument, nullptr, OPTION_SITEMAP},
        {"rt",      required_argument, nullptr, OPTION_RT},
        {"ldfix",   required_argument, nullptr, OPTION_LDFIX},
        {"ldfix-base", required_argument, nullptr, OPTION_LDFIXBASE},
        {"rt-base", required_argument, nullptr, OPTION_RTBASE},
        {nullptr,   no_argument,       nullptr, 0}
    };
    char * const *argv = cxt->argv->data();
    int argc = (int)cxt->argv->size();
    const char *sites_file = nullptr, *map_file = nullptr, *rt_file = nullptr;
    const char *ldfix_file = nullptr;
    intptr_t ldfix_base = 0x60000000;
    // Where the injected runtime ELF is mapped.  0x70000000 is fine for a normal
    // image, but `rewrite.py --ld-so' puts E9Patch's own loader just above the
    // (tiny) ld.so image and e9patch requires --loader-base >= every mapping's
    // end, so a --gt-all ld.so needs the runtime moved down next to the ldfix
    // ELF (see rewrite.py's --ld-so branch).
    intptr_t rt_base = 0x70000000;
    optind = 1;
    while (true)
    {
        int idx;
        int opt = getopt_long_only(argc, argv, "", long_options, &idx);
        if (opt < 0) break;
        switch (opt)
        {
            case OPTION_SITES:   sites_file = optarg; break;
            case OPTION_SITEMAP: map_file   = optarg; break;
            case OPTION_RT:      rt_file    = optarg; break;
            case OPTION_LDFIX:   ldfix_file = optarg; break;
            case OPTION_RTBASE:
                rt_base = (intptr_t)strtoll(optarg, nullptr, 0); break;
            case OPTION_LDFIXBASE:
                ldfix_base = (intptr_t)strtoll(optarg, nullptr, 0); break;
            default:
                fprintf(stderr, "usage: --plugin=ptlog.so:--sites=FILE "
                    "[--plugin=ptlog.so:--sitemap=FILE]\n");
                exit(EXIT_FAILURE);
        }
    }
    if (sites_file == nullptr)
        error("ptlog: missing `--sites=FILE'");
    parseSiteFile(sites_file);
    if (map_file != nullptr)
    {
        opt_map = fopen(map_file, "w");
        if (opt_map == nullptr)
            error("ptlog: failed to open sitemap \"%s\"", map_file);
        fprintf(opt_map, "#addr,site,when,kind,arg,body_offset\n");
    }
    {
        // The runtime data area, reserved read-write by E9Patch's loader:
        //   [counterbase, counterbase + 8*ncounters)  keyframe countdown
        //                                             counters, filled with 1 so
        //                                             the FIRST execution of a
        //                                             guard logs and re-arms
        // rewrite.py sizes and places it (`ncounters').
        size_t n_val = 0;
        bool any_kf = false;
        for (const auto &kv: sites)
            for (const Op &op: kv.second.ops)
            {
                n_val += bufSlots(op);
                if (op.kf > 1) any_kf = true;
            }
        if (opt_nslots > 0)
            error("ptlog: the site file asks for %ld cache slots, which this "
                "plugin does not support (`nslots' must be 0)", opt_nslots);
        long ncnt   = (opt_ncounters >= 0? opt_ncounters
                                         : (any_kf? (long)n_val: 0));
        if (!opt_counterbase) opt_counterbase = opt_slotbase;
        opt_ncounters = ncnt;
        if (ncnt > 0)
        {
            intptr_t lo = opt_counterbase, hi = opt_counterbase + 8 * ncnt;
            size_t len = ((size_t)(hi - lo) + 4095) & ~(size_t)4095;
            std::vector<uint8_t> init(len, 0);
            for (long i = 0; i < ncnt; i++)
                init[8 * (size_t)i] = 1;                // little-endian 1
            sendReserveMessage(cxt->out, lo, init.data(), len,
                PROT_READ | PROT_WRITE, /*init=*/0, /*fini=*/0, /*mmap=*/0,
                /*absolute=*/false);
            fprintf(stderr, "ptlog: data area 0x%" PRIxPTR "..0x%" PRIxPTR
                " (%zu bytes): %ld keyframe counters at 0x%" PRIxPTR "\n",
                lo, lo + (intptr_t)len, len, ncnt, opt_counterbase);
        }
    }
    if (ldfix_file != nullptr)
    {
        // runtime/rt/ldfix.c: restores AT_ENTRY in the auxiliary vector so that
        // a rewritten ld.so still recognises itself as the program being run.
        // Injected at a different base than the buffer-sink runtime so the two
        // can coexist.
        ELF *fix = parseELF(ldfix_file, ldfix_base);
        sendELFFileMessage(cxt->out, fix);
    }
    // The buffer sink needs the runtime; so does a `gt' build, whatever its
    // value sink -- the gt ring lives in the same per-thread %gs block.
    if (opt_sink == SINK_BUFFER || opt_gt)
    {
        // The buffer sink needs the %gs base, the guard-page SIGSEGV handler and
        // the cv-file writer: inject runtime/rt/ptlogrt (built with
        // e9compile.sh -DPTLOG_E9RT) so that its init()/fini() run in the
        // rewritten process.  Without --rt the caller must LD_PRELOAD
        // runtime/rt/ptlogrt.so instead.
        if (rt_file != nullptr)
        {
            ELF *rt = parseELF(rt_file, rt_base);
            sendELFFileMessage(cxt->out, rt);
        }
        else
            warning("ptlog: sink=buffer/gt without `--rt=FILE'; the rewritten "
                "binary will need LD_PRELOAD=ptlogrt.so");
    }
    if (opt_sync_tnt && (opt_sink != SINK_BUFFER || opt_sync <= 0))
        error("ptlog: synccarrier tnt requires the buffer sink and sync > 0");
    fprintf(stderr, "ptlog: %zu instrumented instructions, sink=%s, space=%d, "
        "sync=%ld\n", sites.size(),
        (opt_sink == SINK_BUFFER? "buffer": "ptwrite"), opt_space, opt_sync);
    return nullptr;
}

extern "C" intptr_t e9_plugin_match(const Context *cxt)
{
    if (sites.find(cxt->I->address) != sites.end())
        return 1;
    // GROUND TRUTH: every memory-accessing instruction, not only the spec's
    // critical-value sites.
    if (opt_gt)
    {
        Mem m;
        if (gtForm(cxt->I, m) != GT_NONE)
            return 1;
        if (!gt_skip.empty() && gt_skip.count(cxt->I->address) != 0)
            { n_gt_skipped++; return 0; }
        for (uint8_t i = 0; i < cxt->I->count.op; i++)
            if (cxt->I->op[i].type == OPTYPE_MEM)
                { n_gt_skip++; break; }
    }
    return 0;
}

extern "C" void e9_plugin_code(const Context *cxt)
{
    fputs("\"$ptlog\",", cxt->out);
}

extern "C" void e9_plugin_patch(const Context *cxt)
{
    const InstrInfo *I = cxt->I;
    g_patch_addr = I->address;
    static const Site no_site;
    auto i = sites.find(I->address);
    // A gt build patches instructions the spec never named; they have no ops.
    Mem gtmem;
    GtForm gtf = (opt_gt? gtForm(I, gtmem): GT_NONE);
    if (i == sites.end() && gtf == GT_NONE)
        error("ptlog: patch requested for un-listed address 0x%" PRIxPTR,
            I->address);
    const Site &site = (i != sites.end()? i->second: no_site);

    Mem mem;
    bool have_mem = false;
    const char *why = "";
    for (const Op &op: site.ops)
    {
        if (op.kind != 'm') continue;
        have_mem = getMem(I, mem, &why);
        if (!have_mem)
            error("ptlog: memop site at 0x%" PRIxPTR " cannot be encoded (%s)",
                I->address, why);
        break;
    }

    // Registers the trampolines at THIS instruction must not clobber: every
    // register any of its ops logs (in either phase -- a `before' run must not
    // destroy a value an `after' run logs) plus the base/index of its memory
    // operand.  Everything else that the site's `dead_regs' offers is scratch.
    unsigned site_avoid = 0, site_hard = 1u << 4;      // %rsp is always hard
    for (const Op &op: site.ops)
        if (op.kind == 'r' && op.reg >= 0 && op.reg < 16)
            site_avoid |= 1u << op.reg;
    if (have_mem && !mem.rip)
    {
        // The base/index of the memory operand are HARD: the trampoline
        // dereferences them, so no stack copy can stand in for them.  The
        // registers the site LOGS are soft -- see `Scratch'.
        if (mem.base  >= 0) { site_avoid |= 1u << mem.base;  site_hard |= 1u << mem.base;  }
        if (mem.index >= 0) { site_avoid |= 1u << mem.index; site_hard |= 1u << mem.index; }
    }
    // The gt block is emitted BEFORE anything else and restores every register
    // it touches, so it does NOT have to avoid the registers this site LOGS --
    // only the ones whose value it must still be able to read while computing
    // the effective address.  (Avoiding the logged set too made `libc.so.6'
    // unbuildable: `build_wcs_upper_buffer' at 0xfc317 logs 15 GP registers and
    // indexes its memory operand with the 16th, so nothing would be left.)
    unsigned gt_avoid = 0;
    if (gtf == GT_MEM && !gtmem.rip)
    {
        if (gtmem.base  >= 0) gt_avoid |= 1u << gtmem.base;
        if (gtmem.index >= 0) gt_avoid |= 1u << gtmem.index;
    }
    if (gtf == GT_RBP) gt_avoid |= 1u << 5;         // %rbp is the address
    bool site_pushed = false;

    Emitter e;
    struct MapEnt { long site; char when; char kind; std::string arg; long off;
                    long cnt; long kfper; long kfbr; long kfjoin; };
    std::vector<MapEnt> ents;

    long before_len = -1;
    if (gtf != GT_NONE)
    {
        // ONE trampoline per instruction: the gt record goes in front of
        // whatever critical values this instruction also logs, and the two
        // streams stay independent.
        emitGt(e, gtf, gtmem, I->address, gt_avoid);
        ents.push_back({-1, 'B', 'g', std::string("gt"), e.log_offs.back(),
                        0, 0, -1, -1});
        site_pushed = true;             // it always takes a scratch register
    }
    for (int phase = 0; phase < 2; phase++)
    {
        if (phase == 1)
        {
            before_len = e.body_len;    // byte offset of "$instr" in the body
            e.instr();
            if (gtRep(gtf)) emitGtRepCommit(e);
        }
        std::vector<const Op *> ops;
        for (const Op &op: site.ops)
            if ((int)op.after == phase) ops.push_back(&op);
        for (size_t oi = 0; oi < ops.size(); )
        {
            // A `resync' site's registers share ONE keyframe counter, so they
            // are all logged in the same execution and the reconstructor
            // re-anchors the whole loop-carried set at once.  Gather the run of
            // consecutive ops that belong to it.
            const Op &op0 = *ops[oi];
            const bool group = (op0.resync && op0.kf > 1);
            const Sink run_sink = opt_sink;
            size_t oj = oi + 1;
            if (group)
                while (oj < ops.size() && ops[oj]->resync &&
                       ops[oj]->site == op0.site && ops[oj]->kf == op0.kf)
                    oj++;
            else
                // Everything else at this phase is emitted as ONE run: the
                // trampoline's scratch registers, its red-zone step and (in
                // the buffer sink) its cursor load and cursor update are then
                // shared by every value the site logs.
                while (oj < ops.size() && !ops[oj]->resync)
                    oj++;

            std::vector<size_t> counts;
            auto emit_run = [&](Emitter &x)
            {
                // One `Scratch' per run: the red-zone step and any push/pop
                // are emitted at most once for the whole run, and every
                // register comes from the site's `dead_regs' when it can.
                bool flags_dead = true;
                for (size_t k = oi; k < oj; k++)
                    if (!ops[k]->dead) flags_dead = false;
                unsigned dead  = runDead(ops, oi, oj);
                unsigned avoid = site_avoid;
                if (run_sink == SINK_BUFFER && opt_sync > 0 && !flags_dead)
                    avoid |= 1u << SCR_CNT;     // the countdown needs `jrcxz'
                Scratch S;
                sInit(S, x, dead, avoid, site_hard);
                if (run_sink == SINK_BUFFER)
                    emitBufRun(x, S, ops, oi, oj, mem, flags_dead);
                else
                {
                    for (size_t k = oi; k < oj; k++)
                    {
                        const Op &op = *ops[k];
                        if (op.kind == 'i')
                            error("ptlog: an immediate (`i') op cannot use the "
                                "PTWRITE sink -- `ptwrite' takes r/m64, there "
                                "is no immediate form.  Build with "
                                "`--sink buffer'.");
                        else if (op.kind == 'r') emitPtwOpReg(x, S, op.reg);
                        else                emitPtwOpMem(x, S, mem, op.size);
                    }
                }
                sFini(S);
                if (S.npushed != 0 || S.moved) site_pushed = true;
                for (size_t k = oi; k < oj; k++)
                    counts.push_back((size_t)bufSlots(*ops[k]));
            };
            size_t n0 = e.log_offs.size();
            if (group)
                emitKeyframeGuarded(e, op0.kf, emit_run);
            else
                emit_run(e);

            // One map entry per logged VALUE, at the EXACT byte offset of the
            // instruction that logs it (an xmm register contributes two, `lo'
            // then `hi').  rewrite.py needs no disassembly heuristic for these.
            size_t idx = n0;
            for (size_t k = oi; k < oj; k++)
            {
                const Op &op = *ops[k];
                std::string arg = (op.kind == 'r'? std::string(regName(op.reg))
                                 : op.kind == 'i'? std::to_string(op.imm)
                                                 : std::to_string(op.size));
                size_t n = counts[k - oi];
                for (size_t h = 0; h < n; h++, idx++)
                {
                    std::string a = arg;
                    if (n == 2) a += (h == 0? ".lo": ".hi");
                    ents.push_back({op.site, (op.after? 'A': 'B'), op.kind, a,
                        e.log_offs[idx], e.log_cnt[idx], e.log_kfper[idx],
                        e.log_kfbr[idx], e.log_kfjoin[idx]});
                }
            }
            oi = oj;
        }
    }

    if (site_pushed) n_site_push++; else n_site_free++;

    // Emit the "$ptlog" macro definition for this instruction.
    fputs("\"$ptlog\":[", cxt->out);
    bool first = true;
    for (const Chunk &c: e.chunks)
    {
        switch (c.kind)
        {
            case CH_BYTES:
                for (uint8_t b: c.bytes)
                {
                    fprintf(cxt->out, "%s%u", (first? "": ","), (unsigned)b);
                    first = false;
                }
                break;
            case CH_INSTR:
                fprintf(cxt->out, "%s\"$instr\"", (first? "": ","));
                first = false;
                break;
            case CH_REL32:
                fprintf(cxt->out, "%s{\"rel32\":\"0x%" PRIxPTR "\"}",
                    (first? "": ","), c.target);
                first = false;
                break;
        }
    }
    fputs("],", cxt->out);

    if (opt_map != nullptr)
    {
        // Five pseudo-rows describe the body layout so that rewrite.py can
        // compute every address exactly (offsets are relative to the body and
        // do NOT count the "$instr" macro; anything at or past `before_len'
        // therefore has to be shifted by however many bytes E9Patch's copy of
        // the original instruction actually occupies):
        //   site -1 "I"  byte offset of "$instr"
        //   site -2 "E"  total plugin-emitted body length (the E9Patch "$BREAK"
        //                tail -- copies of the FOLLOWING original instructions
        //                -- starts here)
        //   site -3 "S"  a buffer-sink sync marker `ptwrite <count>'
        //   site -4 "P"  the first PATTERN_MAX bytes of the body BEFORE "$instr"
        //   site -5 "Q"  the first PATTERN_MAX bytes of the body AFTER  "$instr"
        //
        // P and Q exist because the length of E9Patch's copy of the original
        // instruction is NOT the length of one decoded instruction: a `call' is
        // *emulated* by up to five instructions (`e9x86_64.cpp:pushReturnAddress'
        // + the transfer), so decoding one instruction at `before_len' would
        // under-measure the shift and every logged value AFTER the original
        // instruction would be recorded at the wrong trampoline address.  With
        // Q the scanner LOCATES the resumption of the plugin's body instead of
        // guessing.  A `..' pair is a wildcard byte (a {"rel32":...} field,
        // which E9Patch fills in).
        std::string pre, post;
        {
            bool seen_instr = false;
            for (const Chunk &c: e.chunks)
            {
                std::string &dst = (seen_instr? post: pre);
                switch (c.kind)
                {
                    case CH_INSTR: seen_instr = true; break;
                    case CH_REL32:
                        if (dst.size() < 2*PATTERN_MAX) dst += "........";
                        break;
                    case CH_BYTES:
                        for (uint8_t b: c.bytes)
                        {
                            if (dst.size() >= 2*PATTERN_MAX) break;
                            char h[3]; snprintf(h, sizeof h, "%02x", b);
                            dst += h;
                        }
                        break;
                }
            }
            if (pre.size()  > 2*PATTERN_MAX) pre.resize(2*PATTERN_MAX);
            if (post.size() > 2*PATTERN_MAX) post.resize(2*PATTERN_MAX);
        }
        fprintf(opt_map, "0x%" PRIxPTR ",-1,I,i,%ld,%ld\n", I->address,
            before_len, before_len);
        fprintf(opt_map, "0x%" PRIxPTR ",-2,E,e,%ld,%ld\n", I->address,
            e.body_len, e.body_len);
        fprintf(opt_map, "0x%" PRIxPTR ",-4,P,p,%s,%ld\n", I->address,
            pre.empty()? "-": pre.c_str(), before_len);
        fprintf(opt_map, "0x%" PRIxPTR ",-5,Q,q,%s,%ld\n", I->address,
            post.empty()? "-": post.c_str(), e.body_len - before_len);
        for (long o: e.sync_offs)
            fprintf(opt_map, "0x%" PRIxPTR ",-3,S,s,%ld,%ld\n", I->address, o, o);
        for (size_t i = 0; i < e.tnt_offs.size(); i++)      // `synccarrier tnt' only
            fprintf(opt_map, "0x%" PRIxPTR ",-6,T,t,%c,%ld\n", I->address,
                e.tnt_role[i], e.tnt_offs[i]);
        // A keyframe-guarded value carries a KEYED extra column after the six
        // fixed ones; a consumer that only knows the six-column form ignores
        // it:  k=<counter>:<K>:<jnz>:<join>
        for (const MapEnt &m: ents)
        {
            fprintf(opt_map, "0x%" PRIxPTR ",%ld,%c,%c,%s,%ld",
                I->address, m.site, m.when, m.kind, m.arg.c_str(), m.off);
            if (m.kfbr >= 0)
                fprintf(opt_map, ",k=0x%lx:%ld:%ld:%ld",
                    (unsigned long)m.cnt, m.kfper, m.kfbr, m.kfjoin);
            fputc('\n', opt_map);
        }
    }
    n_patched++;
}

extern "C" void e9_plugin_fini(const Context *cxt)
{
    if (opt_map != nullptr)
    {
        fclose(opt_map);
        opt_map = nullptr;
    }
    fprintf(stderr, "ptlog: patched %ld/%zu instructions, %ld logging "
        "instructions emitted, %ld keyframe counters (%ld resync sites)\n",
        n_patched, sites.size(), n_emitted, n_counters, n_kf_resync);
    fprintf(stderr, "ptlog: liveness=%d, push/pop-free sites %ld/%ld (%.1f %%), "
        "scratch registers %ld dead / %ld pushed (%ld borrowed from the site's own "
        "logged registers)\n", (int)opt_liveness,
        n_site_free, n_site_free + n_site_push,
        (n_site_free + n_site_push? 100.0*(double)n_site_free /
            (double)(n_site_free + n_site_push): 0.0),
        n_scr_dead, n_scr_push, n_scr_borrow);
    if (opt_gt)
        fprintf(stderr, "ptlog: gt: %ld ground-truth site(s) (%ld explicit "
            "memory operand, %ld implicit stack operand), %ld "
            "memory-accessing instruction(s) refused, cursor at %%gs:%d\n",
            n_gt_sites, n_gt_mem, n_gt_stack, n_gt_skip, opt_gtoff);
    if (opt_gt && !gt_skip.empty())
        fprintf(stderr, "ptlog: gt: skip list of %zu address(es) dropped the gt "
            "sequence at %ld candidate(s)\n", gt_skip.size(), n_gt_skipped);
}
