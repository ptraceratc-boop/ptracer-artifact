// vexlift.h -- lift ONE x86-64 instruction to VEX IR (via libpyvex's vex_lift) and compile the
// IRSB into a compact, self-owned representation (VEX's arena is reset on the next lift).
// PTracer Stage 3 interprets this "program skeleton" per instruction.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "vexval.h"
extern "C" {
#include <libvex.h>
}

struct IRNodeC {                      // expression node (tree, children by index)
    enum K : uint8_t { CONST, GET, GETI, RDTMP, UNOP, BINOP, TRIOP, QOP, LOAD, ITE, CCALL, UNK } k = UNK;
    IRType ty = Ity_INVALID;
    IROp op = Iop_INVALID;
    int a[4] = {-1, -1, -1, -1};      // children
    int off = 0, size = 0;            // GET: guest offset/size ; RDTMP: tmp index in off
    uint64_t c[4] = {0, 0, 0, 0};     // CONST bytes (up to 32)
    std::string ccname;               // CCALL helper name
    uint8_t ccid = 0;                 // 1 calculate_condition, 2 calculate_rflags_c, 3 calculate_rflags_all, 0 other (set at lift)
    bool cargs_atomic = false;        // every arg is CONST/GET/RDTMP: evaluating them has no side effect
    std::vector<int> cargs;           // CCALL args
};
struct IRStmtC {
    enum K : uint8_t { NOP, IMARK, PUT, PUTI, WRTMP, STORE, STOREG, LOADG, CAS, LLSC, DIRTY, EXIT, MBE } k = NOP;
    int off = 0, size = 0, tmp = -1;  // PUT offset/size; WRTMP/LOADG/CAS/LLSC/DIRTY dst tmp (CAS: oldLo; tmp2 = oldHi)
    int tmp2 = -1;
    int e[6] = {-1, -1, -1, -1, -1, -1}; // expr children: STORE(addr,data) STOREG(addr,data,guard) LOADG(addr,alt,guard)
                                         // CAS(addr,expdLo,dataLo,expdHi,dataHi) LLSC(addr,storedata) DIRTY(guard,mAddr) EXIT(guard)
    int cvt = 0;                      // LOADG conversion (IRLoadGOp)
    uint64_t addr = 0; int len = 0;   // IMARK
    int mfx = 0; int msize = 0;       // DIRTY memory effect (Ifx_*), size
    IRJumpKind jk = Ijk_INVALID; uint64_t dst = 0; // EXIT
};
// Compiled form of one IRBlockC (built lazily by VexInterp on the first execution; the
// "program skeleton"): a flat op list over value slots, with every statement
// whose result can never reach an emitted address or the tracked machine state removed (intra-block
// liveness) and every expression that is unknown by construction folded (see vexinterp.cpp).
struct COp {
    uint8_t k = 0; uint8_t sz = 0; uint16_t aux = 0;
    int32_t dst = 0, a = 0, b = 0, c = 0;   // slot operands (>= 0: slot, < 0: constant -1-i)
    int32_t off = 0;                         // guest offset / size / arg-list start
    uint32_t op = 0;                         // IROp, cvt, ...
    const void* p = nullptr;                 // IRNodeC* (CCALL) or IRStmtC* (complex stmts)
};
struct CProg {                                // ONE allocation: this header, then ops, consts, args
    bool fallback = false;                   // shape not handled: use the tree walker
    int nslots = 0, nops = 0, nconsts = 0, nargs = 0;
    const COp* ops = nullptr;
    const Val* consts = nullptr;
    const int32_t* args = nullptr;           // CCALL argument operands
    int n_dead = 0, n_folded = 0;            // statements removed / expressions folded (stats)
};
struct CProgFree { void operator()(CProg* p) const; };
struct IRBlockC {
    bool ok = false;                  // lifted successfully   (ok/jk/prog first: one cache line per execution)
    IRJumpKind jk = Ijk_INVALID;
    mutable std::unique_ptr<CProg, CProgFree> prog;   // compiled skeleton (VexInterp), null until first exec
    uint64_t addr = 0; int len = 0;   // instruction address/length
    std::vector<IRNodeC> nodes;
    std::vector<IRStmtC> stmts;
    std::vector<IRType> tmpty;
    std::vector<uint8_t> tmpsz;       // Val size of each temp (I1 -> 1), precomputed so exec() does not call sizeofIRType per temp per execution
    int next = -1;                    // expr index of the successor
    std::string err;
};

class VexLifter {
public:
    VexLifter();
    // Lift exactly one instruction at `addr` from `bytes` (>= nbytes available). Returns a block with ok=false on failure.
    std::unique_ptr<IRBlockC> lift_one(uint64_t addr, const uint8_t* bytes, size_t nbytes);
private:
    VexArchInfo ai_;
    int compile_expr(IRBlockC& b, const IRExpr* e);
};
const char* irop_name(IROp op);
