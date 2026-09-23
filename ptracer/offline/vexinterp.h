// vexinterp.h -- interpreter for the per-instruction VEX blocks produced by vexlift.h with a
// known/unknown lattice. This is PTracer's "trace reconstructor": it executes the program
// skeleton, takes critical values from the log, and emits every memory access it can compute.
#pragma once
#include "vexlift.h"
#include <functional>
#include <unordered_map>

struct Val {                      // up to 32 bytes; kn = bitmask of known bytes
    uint64_t w[4] = {0, 0, 0, 0};
    uint32_t kn = 0;
    uint8_t sz = 8;
    static uint32_t mask(int sz) { return sz >= 32 ? 0xffffffffu : ((1u << sz) - 1); }
    bool known() const { return (kn & mask(sz)) == mask(sz); }
    uint64_t u64() const { return sz >= 8 ? w[0] : (w[0] & ((1ull << (8 * sz)) - 1)); }
    static Val K(uint64_t v, int sz) { Val r; r.sz = sz; r.w[0] = sz >= 8 ? v : (v & ((1ull << (8 * sz)) - 1)); r.kn = mask(sz); return r; }
    static Val U(int sz) { Val r; r.sz = sz; return r; }
    uint8_t byte(int i) const { return (uint8_t)(w[i / 8] >> (8 * (i % 8))); }
    void set_byte(int i, uint8_t v, bool k) { w[i / 8] = (w[i / 8] & ~((uint64_t)0xff << (8 * (i % 8)))) | ((uint64_t)v << (8 * (i % 8))); if (k) kn |= 1u << i; else kn &= ~(1u << i); }
};

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
    // Forget `size' bytes at `addr' (an un-liftable instruction stored there: the shadow no longer
    // knows what is in that memory, but everything else stays valid).
    void shadow_forget(uint64_t addr, int size) { for (int i = 0; i < size;) { uint64_t key = (addr + i) >> 3; int o = (addr + i) & 7; int n = 8 - o; if (n > size - i) n = size - i;
            auto it = shadow_.find(key); if (it != shadow_.end()) { for (int j = 0; j < n; j++) it->second.kn &= (uint8_t)~(1 << (o + j)); if (!it->second.kn) shadow_.erase(it); } i += n; } }
    void memop_define(uint64_t addr, uint64_t v, int size) { Val x = Val::K(v, size); shadow_store_force(addr, x, size); }
    // execute one instruction block; `emit` receives each memory access; returns false if the block was not ok
    bool exec(const IRBlockC& b, const std::function<void(const MemAccess&)>& emit);
    // statistics
    uint64_t n_unknown_ops = 0, n_ccall_unknown = 0, n_ccall_known = 0;
    uint64_t stack_lo = 0, stack_hi = 0;   // shadow-store acceptance window (0 = around rsp)
    // register helpers (VEX offsets)
    static int reg_off(const std::string& name, int* size);
private:
    std::vector<Val> t_;
    const std::function<void(const MemAccess&)>* emit_ = nullptr;
    void emit_acc(const Val& addr, int size, int op) { MemAccess m{addr.known() ? addr.u64() : 0, addr.known(), size, op}; if (emit_) (*emit_)(m); }
    struct Chunk { uint8_t v[8]; uint8_t kn; };            // 8 bytes of memory + known mask, keyed by addr>>3
    std::unordered_map<uint64_t, Chunk> shadow_;
    void shadow_store_force(uint64_t addr, const Val& v, int size);
    Val eval(const IRBlockC& b, int ni);
    Val unop(IROp op, const Val& a);
    Val binop(IROp op, const Val& a, const Val& c);
    Val ccall(const IRBlockC& b, const IRNodeC& n);
    bool in_stack_window(uint64_t addr) const;
};
