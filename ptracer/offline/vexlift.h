// vexlift.h -- lift ONE x86-64 instruction to VEX IR (via libpyvex's vex_lift) and compile the
// IRSB into a compact, self-owned representation (VEX's arena is reset on the next lift).
// The reconstructor interprets this "program skeleton" per instruction.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
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
struct IRBlockC {
    uint64_t addr = 0; int len = 0;   // instruction address/length
    std::vector<IRNodeC> nodes;
    std::vector<IRStmtC> stmts;
    std::vector<IRType> tmpty;
    IRJumpKind jk = Ijk_INVALID;
    int next = -1;                    // expr index of the successor
    bool ok = false;                  // lifted successfully
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
