// vexinterp.h -- interpreter for the per-instruction VEX blocks produced by vexlift.h with a
// known/unknown lattice. This is PTracer's "trace reconstructor": it executes the program
// skeleton, takes critical values from the log, and emits every memory access it can compute.
#pragma once
#include "vexlift.h"
#include <functional>
#include <unordered_map>

struct MemAccess { uint64_t addr; bool known; int size; int op; /* 0 load 1 store 2 rmw */ };

class VexInterp {
public:
    VexInterp();
    // guest state (VEX AMD64 layout, 1024 bytes)
    uint8_t g[1024]; uint8_t gk[1024];
    void set_reg(int off, int size, uint64_t v);       // define a register (known)
    void set_reg_unknown(int off, int size);
    Val get_reg(int off, int size) const;
    void all_unknown();                                 // e.g. after a PT overflow
    // Guest state that is an ABI invariant rather than something the value log carries: the
    // direction flag (see vexinterp.cpp).  Applied at construction and after all_unknown().
    void init_invariants();
    // shadow memory (stack slots + logged memop values)
    void shadow_store(uint64_t addr, const Val& v, int size);
    Val shadow_load(uint64_t addr, int size) const;
    void shadow_clear() { shadow_.clear(); }
    size_t shadow_chunks() const { return shadow_.size(); }
    // Forget `size' bytes at `addr' (an un-liftable instruction stored there: the shadow no longer
    // knows what is in that memory, but everything else stays valid).
    void shadow_forget(uint64_t addr, int size) { for (int i = 0; i < size;) { uint64_t key = (addr + i) >> 3; int o = (addr + i) & 7; int n = 8 - o; if (n > size - i) n = size - i;
            auto it = shadow_.find(key); if (it != shadow_.end()) { for (int j = 0; j < n; j++) it->second.kn &= (uint8_t)~(1 << (o + j)); if (!it->second.kn) shadow_.erase(it); } i += n; } }
    void memop_define(uint64_t addr, uint64_t v, int size) { Val x = Val::K(v, size); shadow_store_force(addr, x, size); }
    // execute one instruction block; `emit` receives each memory access; returns false if the block was not ok
    bool exec(const IRBlockC& b, const std::function<void(const MemAccess&)>& emit);
    // Same, appending the accesses to `sink' (nullptr: discard them).  Uses the compiled skeleton
    // (compile()) unless PTRECON_INTERP=tree or the block's shape is not handled.
    bool exec(const IRBlockC& b, std::vector<MemAccess>* sink);
    static bool tree_mode();                            // PTRECON_INTERP=tree: always the tree walker
    uint64_t n_prog = 0, n_prog_fallback = 0, n_stmt_dead = 0, n_expr_folded = 0;   // compile stats
    // ---- fused programs (a run of instructions compiled into one op list) -------------
    // Each instruction's compiled skeleton is concatenated with its slots/constants renumbered
    // into one slot space (constants preloaded, never written), an OP_MARK per instruction records
    // where its memory accesses start in the sink, and a backward pass over the WHOLE run removes
    // guest-state writes that a later instruction of the run overwrites before anything reads
    // them (the flags thunk of nearly every ALU instruction), then the computations only they fed.
    // Guest state, shadow memory and the access sequence after the run equal running the
    // instructions one by one; nothing outside reads state inside the run.
    struct FProg { std::vector<COp> ops; std::vector<int32_t> args; std::vector<Val> slots; int ninsn = 0; uint64_t gen = 0; int ops_in = 0; int nemit = 0; };
    bool build_fused(const IRBlockC* const* blks, int n, FProg& F);
    // writes the run's accesses to a buffer it owns (*acc) and instruction i's first access index to marks[i] (marks[ninsn] = total)
    void exec_fused(FProg& F, const MemAccess** acc, uint32_t* marks);
    std::vector<MemAccess> fbuf_;
    uint64_t n_fused = 0, n_fused_ops_in = 0, n_fused_ops_out = 0;
    // statistics
    uint64_t n_unknown_ops = 0, n_ccall_unknown = 0, n_ccall_known = 0;
    uint64_t stack_lo = 0, stack_hi = 0;   // shadow-store acceptance window (0 = around rsp)
    // register helpers (VEX offsets)
    static int reg_off(const std::string& name, int* size);
private:
    std::vector<Val> t_;
    const std::function<void(const MemAccess&)>* emit_ = nullptr;
    std::vector<MemAccess>* sink_ = nullptr; bool use_sink_ = false; bool tree_ = false;
    void emit_acc(const Val& addr, int size, int op) { MemAccess m{addr.known() ? addr.u64() : 0, addr.known(), size, op};
        if (use_sink_) { if (sink_) sink_->push_back(m); } else if (emit_) (*emit_)(m); }
    bool exec_tree(const IRBlockC& b);
    void compile(const IRBlockC& b);
    void run_prog(const CProg& p);
    Val ccall_args(const IRNodeC& n, Val* argbuf, size_t nargs);
    struct Chunk { uint8_t v[8]; uint8_t kn; };            // 8 bytes of memory + known mask, keyed by addr>>3
    std::unordered_map<uint64_t, Chunk> shadow_;
    void shadow_store_force(uint64_t addr, const Val& v, int size);
    Val eval(const IRBlockC& b, int ni);
    Val unop(IROp op, const Val& a);
    Val binop(IROp op, const Val& a, const Val& c);
    Val ccall(const IRBlockC& b, const IRNodeC& n);
    bool in_stack_window(uint64_t addr) const;
};
