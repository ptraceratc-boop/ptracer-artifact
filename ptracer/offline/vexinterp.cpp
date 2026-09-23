// vexinterp.cpp -- see vexinterp.h. Known/unknown-lattice evaluation of VEX IR for address
// reconstruction. Anything not needed for address arithmetic degrades to "unknown" (never wrong).
#include "vexinterp.h"
#include <cstring>
#include <emmintrin.h>
#include <libvex_guest_offsets.h>

// Eight known-bits at a time.  `gk' holds one 0/1 byte per guest byte, and a byte-at-a-time
// loop here is a measurable share of the whole reconstruction: every GET of a register and
// every effective-address computation goes through it.  The 8-byte groups are read with one load each; the remainder keeps the old
// byte loop.  Bit-for-bit identical to the loop by construction: `_mm_cmpgt_epi8' against zero
// is exactly `k[i] != 0' for the 0/1 bytes the guest-state mask is made of.
static inline uint32_t kn_mask8(const uint8_t* k) {
    __m128i x = _mm_loadl_epi64((const __m128i*)k);
    return (uint32_t)_mm_movemask_epi8(_mm_cmpgt_epi8(x, _mm_setzero_si128())) & 0xffu;
}
static inline Val from_bytes(const uint8_t* v, const uint8_t* k, int size) {
    Val r; r.sz = size; r.kn = 0;
    int i = 0;
    for (; i + 8 <= size; i += 8) { memcpy(&r.w[i / 8], v + i, 8); r.kn |= kn_mask8(k + i) << i; }
    for (; i < size; i++) { r.w[i / 8] |= (uint64_t)v[i] << (8 * (i % 8)); if (k[i]) r.kn |= 1u << i; }
    return r;
}
static inline int ty_size(IRType t) { return t == Ity_I1 ? 1 : sizeofIRType(t); }

// The x86-64 psABI requires DF = 0 at every function boundary, and every compiler and libc keeps
// it that way (`std' is only ever used in a `std'/`cld' pair, whose writes VEX models and this
// interpreter therefore sees).  VEX represents it as `guest_DFLAG' = +1 (DF clear) or -1, and it
// is what a string instruction adds to %rdi/%rsi after every element -- so leaving it UNKNOWN
// makes the SECOND and every later iteration of an expanded `rep stos'/`rep movs' compute an
// unknown address, even when %rdi, %rsi and %rcx are all known (defect D-U10, measured on
// whole-program CPython start-up: 312 728 of the 312 760 records of one `rep stosb' in
// __memset_avx2_unaligned_erms).  It is therefore initialised, and restored after a state loss,
// exactly like the TLS base.
void VexInterp::init_invariants() { set_reg(OFFSET_amd64_DFLAG, 8, 1); }
VexInterp::VexInterp() { memset(g, 0, sizeof g); memset(gk, 0, sizeof gk); init_invariants(); }
void VexInterp::set_reg(int off, int size, uint64_t v) { for (int i = 0; i < size && off + i < 1024; i++) { g[off + i] = (uint8_t)(v >> (8 * i)); gk[off + i] = 1; } }
void VexInterp::set_reg_unknown(int off, int size) { for (int i = 0; i < size && off + i < 1024; i++) gk[off + i] = 0; }
Val VexInterp::get_reg(int off, int size) const { if (off < 0 || off + size > 1024) return Val::U(size); return from_bytes(g + off, gk + off, size); }
void VexInterp::all_unknown() { memset(gk, 0, sizeof gk); shadow_.clear(); init_invariants(); }

int VexInterp::reg_off(const std::string& n, int* size) {
    static const struct { const char* n; int off; int sz; } T[] = {
        {"rax", OFFSET_amd64_RAX, 8}, {"rcx", OFFSET_amd64_RCX, 8}, {"rdx", OFFSET_amd64_RDX, 8}, {"rbx", OFFSET_amd64_RBX, 8},
        {"rsp", OFFSET_amd64_RSP, 8}, {"rbp", OFFSET_amd64_RBP, 8}, {"rsi", OFFSET_amd64_RSI, 8}, {"rdi", OFFSET_amd64_RDI, 8},
        {"r8", OFFSET_amd64_R8, 8}, {"r9", OFFSET_amd64_R9, 8}, {"r10", OFFSET_amd64_R10, 8}, {"r11", OFFSET_amd64_R11, 8},
        {"r12", OFFSET_amd64_R12, 8}, {"r13", OFFSET_amd64_R13, 8}, {"r14", OFFSET_amd64_R14, 8}, {"r15", OFFSET_amd64_R15, 8},
        {"rip", OFFSET_amd64_RIP, 8}, {"fs_base", OFFSET_amd64_FS_CONST, 8}, {"gs_base", -1, 8},
        {"xmm0", OFFSET_amd64_YMM0, 16}, {"xmm1", OFFSET_amd64_YMM1, 16}, {"xmm2", OFFSET_amd64_YMM2, 16}, {"xmm3", OFFSET_amd64_YMM3, 16},
        {"xmm4", OFFSET_amd64_YMM4, 16}, {"xmm5", OFFSET_amd64_YMM5, 16}, {"xmm6", OFFSET_amd64_YMM6, 16}, {"xmm7", OFFSET_amd64_YMM7, 16},
        {"xmm8", OFFSET_amd64_YMM8, 16}, {"xmm9", OFFSET_amd64_YMM9, 16}, {"xmm10", OFFSET_amd64_YMM10, 16}, {"xmm11", OFFSET_amd64_YMM11, 16},
        {"xmm12", OFFSET_amd64_YMM12, 16}, {"xmm13", OFFSET_amd64_YMM13, 16}, {"xmm14", OFFSET_amd64_YMM14, 16}, {"xmm15", OFFSET_amd64_YMM15, 16},
    };
    for (auto& t : T) if (n == t.n) { if (size) *size = t.sz; return t.off; }
    return -1;
}

bool VexInterp::in_stack_window(uint64_t addr) const {
    if (stack_lo || stack_hi) return addr >= stack_lo && addr < stack_hi;
    Val rsp = get_reg(OFFSET_amd64_RSP, 8);
    if (!rsp.known()) return false;
    uint64_t sp = rsp.u64();
    return addr + 4096 >= sp && addr < sp + (16u << 20);
}
void VexInterp::shadow_store_force(uint64_t addr, const Val& v, int size) {
    for (int i = 0; i < size;) {
        uint64_t key = (addr + i) >> 3; int o = (addr + i) & 7; int n = 8 - o; if (n > size - i) n = size - i;
        auto it = shadow_.find(key);
        if (it == shadow_.end()) { bool any = false; for (int j = 0; j < n; j++) if (v.kn & (1u << (i + j))) any = true; if (!any) { i += n; continue; } it = shadow_.emplace(key, Chunk{{0}, 0}).first; }
        Chunk& c = it->second;
        for (int j = 0; j < n; j++) { if (v.kn & (1u << (i + j))) { c.v[o + j] = v.byte(i + j); c.kn |= 1 << (o + j); } else c.kn &= ~(1 << (o + j)); }
        if (!c.kn) shadow_.erase(it);
        i += n;
    }
}
void VexInterp::shadow_store(uint64_t addr, const Val& v, int size) {
    if (!in_stack_window(addr)) { for (int i = 0; i < size;) { uint64_t key = (addr + i) >> 3; int o = (addr + i) & 7; int n = 8 - o; if (n > size - i) n = size - i;
            auto it = shadow_.find(key); if (it != shadow_.end()) { for (int j = 0; j < n; j++) it->second.kn &= ~(1 << (o + j)); if (!it->second.kn) shadow_.erase(it); } i += n; } return; }
    shadow_store_force(addr, v, size);
}
Val VexInterp::shadow_load(uint64_t addr, int size) const {
    Val r = Val::U(size);
    for (int i = 0; i < size;) {
        uint64_t key = (addr + i) >> 3; int o = (addr + i) & 7; int n = 8 - o; if (n > size - i) n = size - i;
        auto it = shadow_.find(key);
        if (it != shadow_.end()) for (int j = 0; j < n; j++) if (it->second.kn & (1 << (o + j))) r.set_byte(i + j, it->second.v[o + j], true);
        i += n;
    }
    return r;
}

/* ---------------- ops ---------------- */
static inline Val widen(const Val& a, int to, bool sign) {
    Val r = Val::U(to); if (!a.known()) return r;
    uint64_t v = a.u64(); int from = a.sz;
    if (sign && from < 8 && (v >> (8 * from - 1)) & 1) v |= ~((1ull << (8 * from)) - 1);
    return Val::K(v, to);
}
static inline Val narrow(const Val& a, int to) { Val r; r.sz = to; r.kn = a.kn & Val::mask(to); r.w[0] = a.w[0]; r.w[1] = a.w[1]; r.w[2] = a.w[2]; r.w[3] = a.w[3]; if (to < 8) r.w[0] &= (1ull << (8 * to)) - 1; return r; }
static inline Val take_bytes(const Val& a, int from, int n) { Val r = Val::U(n); for (int i = 0; i < n; i++) if (from + i < 32) r.set_byte(i, a.byte(from + i), (a.kn >> (from + i)) & 1); return r; }
static inline Val concat(const Val& hi, const Val& lo) { int n = lo.sz; Val r = Val::U(n * 2); for (int i = 0; i < n; i++) { r.set_byte(i, lo.byte(i), (lo.kn >> i) & 1); r.set_byte(n + i, hi.byte(i), (hi.kn >> i) & 1); } return r; }
static inline uint64_t smask(int sz) { return sz >= 8 ? ~0ull : ((1ull << (8 * sz)) - 1); }
static inline int64_t sext(uint64_t v, int sz) { if (sz >= 8) return (int64_t)v; int sh = 64 - 8 * sz; return ((int64_t)(v << sh)) >> sh; }

Val VexInterp::unop(IROp op, const Val& a) {
    switch (op) {
        case Iop_Not1: return a.known() ? Val::K(!a.u64(), 1) : Val::U(1);
        case Iop_Not8: case Iop_Not16: case Iop_Not32: case Iop_Not64: return a.known() ? Val::K(~a.u64() & smask(a.sz), a.sz) : Val::U(a.sz);
        case Iop_1Uto8: return widen(a, 1, false); case Iop_1Uto32: return widen(a, 4, false); case Iop_1Uto64: return widen(a, 8, false);
        case Iop_1Sto8: case Iop_1Sto16: case Iop_1Sto32: case Iop_1Sto64: { int to = op == Iop_1Sto8 ? 1 : op == Iop_1Sto16 ? 2 : op == Iop_1Sto32 ? 4 : 8; return a.known() ? Val::K(a.u64() ? smask(to) : 0, to) : Val::U(to); }
        case Iop_8Uto16: return widen(a, 2, false); case Iop_8Uto32: return widen(a, 4, false); case Iop_8Uto64: return widen(a, 8, false);
        case Iop_8Sto16: return widen(a, 2, true); case Iop_8Sto32: return widen(a, 4, true); case Iop_8Sto64: return widen(a, 8, true);
        case Iop_16Uto32: return widen(a, 4, false); case Iop_16Uto64: return widen(a, 8, false);
        case Iop_16Sto32: return widen(a, 4, true); case Iop_16Sto64: return widen(a, 8, true);
        case Iop_32Uto64: return widen(a, 8, false); case Iop_32Sto64: return widen(a, 8, true);
        case Iop_64to8: case Iop_32to8: case Iop_16to8: return narrow(a, 1);
        case Iop_64to16: case Iop_32to16: return narrow(a, 2);
        case Iop_64to32: return narrow(a, 4);
        case Iop_64HIto32: return take_bytes(a, 4, 4); case Iop_32HIto16: return take_bytes(a, 2, 2); case Iop_16HIto8: return take_bytes(a, 1, 1);
        case Iop_128to64: return take_bytes(a, 0, 8); case Iop_128HIto64: return take_bytes(a, 8, 8);
        case Iop_V128to64: return take_bytes(a, 0, 8); case Iop_V128HIto64: return take_bytes(a, 8, 8); case Iop_V128to32: return take_bytes(a, 0, 4);
        case Iop_V256toV128_0: return take_bytes(a, 0, 16); case Iop_V256toV128_1: return take_bytes(a, 16, 16);
        case Iop_64UtoV128: { Val r = Val::K(0, 16); Val lo = widen(a, 8, false); for (int i = 0; i < 8; i++) r.set_byte(i, lo.byte(i), (lo.kn >> i) & 1); return r; }
        case Iop_32UtoV128: { Val r = Val::K(0, 16); for (int i = 0; i < 4; i++) r.set_byte(i, a.byte(i), (a.kn >> i) & 1); return r; }
        case Iop_32to1: case Iop_64to1: return a.known() ? Val::K(a.u64() & 1, 1) : Val::U(1);
        case Iop_ReinterpF64asI64: case Iop_ReinterpI64asF64: case Iop_ReinterpF32asI32: case Iop_ReinterpI32asF32: return a;
        case Iop_NotV128: case Iop_NotV256: { Val r = a; for (int i = 0; i < 4; i++) r.w[i] = ~r.w[i]; return r; }
        case Iop_CmpNEZ8: case Iop_CmpNEZ16: case Iop_CmpNEZ32: case Iop_CmpNEZ64: return a.known() ? Val::K(a.u64() != 0, 1) : Val::U(1);
        case Iop_Clz64: case Iop_Clz32: { int sz = op == Iop_Clz64 ? 8 : 4; if (!a.known()) return Val::U(sz); uint64_t v = a.u64(); if (!v) return Val::K(8 * sz, sz); int n = 0; for (int i = 8 * sz - 1; i >= 0 && !((v >> i) & 1); i--) n++; return Val::K(n, sz); }
        case Iop_Ctz64: case Iop_Ctz32: { int sz = op == Iop_Ctz64 ? 8 : 4; if (!a.known()) return Val::U(sz); uint64_t v = a.u64(); if (!v) return Val::K(8 * sz, sz); return Val::K(__builtin_ctzll(v), sz); }
        case Iop_Left8: case Iop_Left16: case Iop_Left32: case Iop_Left64: return a.known() ? Val::K((a.u64() | (0 - a.u64())) & smask(a.sz), a.sz) : Val::U(a.sz);
        default: n_unknown_ops++; return Val::U(8);
    }
}

Val VexInterp::binop(IROp op, const Val& a, const Val& c) {
    auto both = [&]() { return a.known() && c.known(); };
    int sz = a.sz;
    switch (op) {
        case Iop_Add8: case Iop_Add16: case Iop_Add32: case Iop_Add64: return both() ? Val::K(a.u64() + c.u64(), sz) : Val::U(sz);
        case Iop_Sub8: case Iop_Sub16: case Iop_Sub32: case Iop_Sub64: return both() ? Val::K(a.u64() - c.u64(), sz) : Val::U(sz);
        case Iop_Mul8: case Iop_Mul16: case Iop_Mul32: case Iop_Mul64: return both() ? Val::K(a.u64() * c.u64(), sz) : Val::U(sz);
        case Iop_And8: case Iop_And16: case Iop_And32: case Iop_And64: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool ka = (a.kn >> i) & 1, kc = (c.kn >> i) & 1; uint8_t x = a.byte(i) & c.byte(i); bool k = (ka && kc) || (ka && !a.byte(i)) || (kc && !c.byte(i)); r.set_byte(i, k ? x : 0, k); } return r; }
        case Iop_Or8: case Iop_Or16: case Iop_Or32: case Iop_Or64: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool ka = (a.kn >> i) & 1, kc = (c.kn >> i) & 1; uint8_t x = a.byte(i) | c.byte(i); bool k = (ka && kc) || (ka && a.byte(i) == 0xff) || (kc && c.byte(i) == 0xff); r.set_byte(i, k ? x : 0, k); } return r; }
        case Iop_Xor8: case Iop_Xor16: case Iop_Xor32: case Iop_Xor64: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool k = ((a.kn >> i) & 1) && ((c.kn >> i) & 1); r.set_byte(i, k ? (a.byte(i) ^ c.byte(i)) : 0, k); } return r; }
        case Iop_Shl8: case Iop_Shl16: case Iop_Shl32: case Iop_Shl64: return both() ? Val::K(c.u64() >= 64 ? 0 : a.u64() << (c.u64() & 63), sz) : Val::U(sz);
        case Iop_Shr8: case Iop_Shr16: case Iop_Shr32: case Iop_Shr64: return both() ? Val::K(c.u64() >= 64 ? 0 : a.u64() >> (c.u64() & 63), sz) : Val::U(sz);
        case Iop_Sar8: case Iop_Sar16: case Iop_Sar32: case Iop_Sar64: return both() ? Val::K((uint64_t)(sext(a.u64(), sz) >> (c.u64() > 63 ? 63 : c.u64())), sz) : Val::U(sz);
        case Iop_CmpEQ8: case Iop_CmpEQ16: case Iop_CmpEQ32: case Iop_CmpEQ64: case Iop_CasCmpEQ8: case Iop_CasCmpEQ16: case Iop_CasCmpEQ32: case Iop_CasCmpEQ64:
            return both() ? Val::K(a.u64() == c.u64(), 1) : Val::U(1);
        case Iop_CmpNE8: case Iop_CmpNE16: case Iop_CmpNE32: case Iop_CmpNE64: case Iop_CasCmpNE8: case Iop_CasCmpNE16: case Iop_CasCmpNE32: case Iop_CasCmpNE64:
        case Iop_ExpCmpNE8: case Iop_ExpCmpNE16: case Iop_ExpCmpNE32: case Iop_ExpCmpNE64:
            return both() ? Val::K(a.u64() != c.u64(), 1) : Val::U(1);
        case Iop_CmpLT32S: case Iop_CmpLT64S: return both() ? Val::K(sext(a.u64(), sz) < sext(c.u64(), sz), 1) : Val::U(1);
        case Iop_CmpLE32S: case Iop_CmpLE64S: return both() ? Val::K(sext(a.u64(), sz) <= sext(c.u64(), sz), 1) : Val::U(1);
        case Iop_CmpLT32U: case Iop_CmpLT64U: return both() ? Val::K(a.u64() < c.u64(), 1) : Val::U(1);
        case Iop_CmpLE32U: case Iop_CmpLE64U: return both() ? Val::K(a.u64() <= c.u64(), 1) : Val::U(1);
        case Iop_MullU8: case Iop_MullU16: case Iop_MullU32: { if (!both()) return Val::U(2 * sz); return Val::K(a.u64() * c.u64(), 2 * sz); }
        case Iop_MullS8: case Iop_MullS16: case Iop_MullS32: { if (!both()) return Val::U(2 * sz); return Val::K((uint64_t)(sext(a.u64(), sz) * sext(c.u64(), sz)), 2 * sz); }
        case Iop_MullU64: { if (!both()) return Val::U(16); unsigned __int128 p = (unsigned __int128)a.u64() * c.u64(); Val r = Val::K(0, 16); r.w[0] = (uint64_t)p; r.w[1] = (uint64_t)(p >> 64); return r; }
        case Iop_MullS64: { if (!both()) return Val::U(16); __int128 p = (__int128)(int64_t)a.u64() * (int64_t)c.u64(); Val r = Val::K(0, 16); r.w[0] = (uint64_t)p; r.w[1] = (uint64_t)((unsigned __int128)p >> 64); return r; }
        case Iop_DivModU64to32: { if (!both() || !c.u64()) return Val::U(8); uint64_t q = a.u64() / c.u64(), m = a.u64() % c.u64(); return Val::K((q & 0xffffffff) | (m << 32), 8); }
        case Iop_DivModS64to32: { if (!both() || !c.u64()) return Val::U(8); int64_t q = (int64_t)a.u64() / sext(c.u64(), 4), m = (int64_t)a.u64() % sext(c.u64(), 4); return Val::K(((uint64_t)q & 0xffffffff) | ((uint64_t)m << 32), 8); }
        case Iop_DivModU128to64: { if (!both() || !c.u64()) return Val::U(16); unsigned __int128 n = ((unsigned __int128)a.w[1] << 64) | a.w[0]; Val r = Val::K(0, 16); r.w[0] = (uint64_t)(n / c.u64()); r.w[1] = (uint64_t)(n % c.u64()); return r; }
        case Iop_DivModS128to64: { if (!both() || !c.u64()) return Val::U(16); __int128 n = (__int128)(((unsigned __int128)a.w[1] << 64) | a.w[0]); Val r = Val::K(0, 16); r.w[0] = (uint64_t)(n / (int64_t)c.u64()); r.w[1] = (uint64_t)(n % (int64_t)c.u64()); return r; }
        case Iop_8HLto16: case Iop_16HLto32: case Iop_32HLto64: case Iop_64HLto128: case Iop_64HLtoV128: case Iop_V128HLtoV256: return concat(a, c);
        case Iop_AndV128: case Iop_AndV256: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool k = ((a.kn >> i) & 1) && ((c.kn >> i) & 1); r.set_byte(i, a.byte(i) & c.byte(i), k); } return r; }
        case Iop_OrV128: case Iop_OrV256: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool k = ((a.kn >> i) & 1) && ((c.kn >> i) & 1); r.set_byte(i, a.byte(i) | c.byte(i), k); } return r; }
        case Iop_XorV128: case Iop_XorV256: { Val r = Val::U(sz); for (int i = 0; i < sz; i++) { bool k = ((a.kn >> i) & 1) && ((c.kn >> i) & 1); r.set_byte(i, a.byte(i) ^ c.byte(i), k); } return r; }
        case Iop_SetV128lo64: { Val r = a; for (int i = 0; i < 8; i++) r.set_byte(i, c.byte(i), (c.kn >> i) & 1); return r; }
        case Iop_SetV128lo32: { Val r = a; for (int i = 0; i < 4; i++) r.set_byte(i, c.byte(i), (c.kn >> i) & 1); return r; }
        case Iop_InterleaveLO64x2: { Val r = Val::U(16); for (int i = 0; i < 8; i++) { r.set_byte(i, c.byte(i), (c.kn >> i) & 1); r.set_byte(8 + i, a.byte(i), (a.kn >> i) & 1); } return r; }
        case Iop_InterleaveHI64x2: { Val r = Val::U(16); for (int i = 0; i < 8; i++) { r.set_byte(i, c.byte(8 + i), (c.kn >> (8 + i)) & 1); r.set_byte(8 + i, a.byte(8 + i), (a.kn >> (8 + i)) & 1); } return r; }
        case Iop_Max32U: return both() ? Val::K(a.u64() > c.u64() ? a.u64() : c.u64(), 4) : Val::U(4);
        default: n_unknown_ops++; return Val::U(sz);
    }
}

/* x86 flag helpers (subset of VEX's guest_amd64_helpers.c) for cmov/setcc/adc/sbb. */
enum { CC_OP_COPY = 0, CC_OP_ADDB = 1, CC_OP_SUBB = 5, CC_OP_ADCB = 9, CC_OP_SBBB = 13, CC_OP_LOGICB = 17, CC_OP_INCB = 21, CC_OP_DECB = 25, CC_OP_SHLB = 29, CC_OP_SHRB = 33 };
static bool calc_flags(uint64_t op, uint64_t d1, uint64_t d2, uint64_t nd, int* cf, int* pf, int* zf, int* sf, int* of) {
    auto parity = [](uint64_t v) { return (__builtin_popcount((unsigned)(v & 0xff)) & 1) == 0; };
    if (op == CC_OP_COPY) { *cf = (d1 >> 0) & 1; *pf = (d1 >> 2) & 1; *zf = (d1 >> 6) & 1; *sf = (d1 >> 7) & 1; *of = (d1 >> 11) & 1; return true; }
    int kind, sz;
    if (op >= CC_OP_ADDB && op < CC_OP_ADDB + 4) { kind = 0; sz = op - CC_OP_ADDB; }
    else if (op >= CC_OP_SUBB && op < CC_OP_SUBB + 4) { kind = 1; sz = op - CC_OP_SUBB; }
    else if (op >= CC_OP_LOGICB && op < CC_OP_LOGICB + 4) { kind = 2; sz = op - CC_OP_LOGICB; }
    else if (op >= CC_OP_INCB && op < CC_OP_INCB + 4) { kind = 3; sz = op - CC_OP_INCB; }
    else if (op >= CC_OP_DECB && op < CC_OP_DECB + 4) { kind = 4; sz = op - CC_OP_DECB; }
    else if (op >= CC_OP_ADCB && op < CC_OP_ADCB + 4) { kind = 5; sz = op - CC_OP_ADCB; }
    else if (op >= CC_OP_SBBB && op < CC_OP_SBBB + 4) { kind = 6; sz = op - CC_OP_SBBB; }
    else if (op >= CC_OP_SHLB && op < CC_OP_SHLB + 4) { kind = 7; sz = op - CC_OP_SHLB; }
    else if (op >= CC_OP_SHRB && op < CC_OP_SHRB + 4) { kind = 8; sz = op - CC_OP_SHRB; }
    else return false;
    int bits = 8 << sz; uint64_t m = bits == 64 ? ~0ull : ((1ull << bits) - 1), sign = 1ull << (bits - 1);
    uint64_t a1 = d1 & m, a2 = d2 & m, res;
    switch (kind) {
        case 0: res = (a1 + a2) & m; *cf = res < a1; *of = ((~(a1 ^ a2) & (a1 ^ res)) & sign) != 0; break;
        case 1: res = (a1 - a2) & m; *cf = a1 < a2; *of = (((a1 ^ a2) & (a1 ^ res)) & sign) != 0; break;
        case 2: res = a1; *cf = 0; *of = 0; break;
        case 3: res = a1; *cf = nd & 1; *of = res == sign; break;
        case 4: res = a1; *cf = nd & 1; *of = res == (sign - 1); break;
        case 5: { uint64_t old = nd & 1; uint64_t x2 = a2 ^ old; res = (a1 + x2 + old) & m; *cf = old ? res <= a1 : res < a1; *of = ((~(a1 ^ x2) & (a1 ^ res)) & sign) != 0; break; }
        case 6: { uint64_t old = nd & 1; uint64_t x2 = a2 ^ old; res = (a1 - x2 - old) & m; *cf = old ? a1 <= x2 : a1 < x2; *of = (((a1 ^ x2) & (a1 ^ res)) & sign) != 0; break; }
        case 7: res = a1; *cf = (a2 >> (bits - 1)) & 1; *of = ((a2 ^ a1) & sign) != 0; break;   // dep2 = res<<1 semantics in VEX: cf = msb of dep2
        case 8: res = a1; *cf = a2 & 1; *of = ((a2 ^ a1) & sign) != 0; break;
        default: return false;
    }
    *zf = res == 0; *sf = (res & sign) != 0; *pf = parity(res);
    return true;
}

Val VexInterp::ccall(const IRBlockC& b, const IRNodeC& n) {
    int rsz = ty_size(n.ty);
    Val argbuf[8]; size_t nargs = n.cargs.size() > 8 ? 8 : n.cargs.size(); for (size_t i = 0; i < nargs; i++) argbuf[i] = eval(b, n.cargs[i]);
    struct { Val* p; size_t n; size_t size() const { return n; } Val& operator[](size_t i) { return p[i]; } } args{argbuf, nargs};
    // flag helpers: (cond,) op, dep1, dep2, ndep -- ndep is only consumed by INC/DEC/ADC/SBB, so an unknown
    // ndep must not poison the common SUB/ADD/LOGIC cases.
    auto flags_of = [&](size_t i0, int* cf, int* pf, int* zf, int* sf, int* of) -> bool {
        if (args.size() < i0 + 4) return false;
        for (size_t i = i0; i < i0 + 3; i++) if (!args[i].known()) return false;
        uint64_t op = args[i0].u64(); bool need_nd = (op >= CC_OP_ADCB && op < CC_OP_LOGICB) || (op >= CC_OP_INCB && op < CC_OP_SHLB);
        if (need_nd && !args[i0 + 3].known()) return false;
        return calc_flags(op, args[i0 + 1].u64(), args[i0 + 2].u64(), args[i0 + 3].known() ? args[i0 + 3].u64() : 0, cf, pf, zf, sf, of);
    };
    int cf, pf, zf, sf, of;
    if (n.ccname == "amd64g_calculate_condition" && args.size() == 5 && args[0].known()) {
        uint64_t cond = args[0].u64();
        if (!flags_of(1, &cf, &pf, &zf, &sf, &of)) { n_ccall_unknown++; return Val::U(rsz); }
        int r;
        switch (cond >> 1) { case 0: r = of; break; case 1: r = cf; break; case 2: r = zf; break; case 3: r = cf | zf; break; case 4: r = sf; break; case 5: r = pf; break; case 6: r = sf ^ of; break; case 7: r = (sf ^ of) | zf; break; default: r = 0; }
        if (cond & 1) r = !r;
        n_ccall_known++; return Val::K(r, rsz);
    }
    if (n.ccname == "amd64g_calculate_rflags_c" && args.size() == 4) {
        if (!flags_of(0, &cf, &pf, &zf, &sf, &of)) { n_ccall_unknown++; return Val::U(rsz); }
        n_ccall_known++; return Val::K(cf, rsz);
    }
    if (n.ccname == "amd64g_calculate_rflags_all" && args.size() == 4) {
        if (!flags_of(0, &cf, &pf, &zf, &sf, &of)) { n_ccall_unknown++; return Val::U(rsz); }
        n_ccall_known++; return Val::K((cf << 0) | (pf << 2) | (zf << 6) | (sf << 7) | (of << 11), rsz);
    }
    n_ccall_unknown++; return Val::U(rsz);
}

Val VexInterp::eval(const IRBlockC& b, int ni) {
    const IRNodeC& n = b.nodes[ni];
    switch (n.k) {
        case IRNodeC::CONST: { Val r = Val::U(n.size); r.kn = Val::mask(n.size); for (int i = 0; i < 4; i++) r.w[i] = n.c[i]; return r; }
        case IRNodeC::GET: return get_reg(n.off, n.size);
        case IRNodeC::RDTMP: { const Val& v = t_[n.off]; return v; }
        case IRNodeC::UNOP: return unop(n.op, eval(b, n.a[0]));
        case IRNodeC::BINOP: return binop(n.op, eval(b, n.a[0]), eval(b, n.a[1]));
        case IRNodeC::ITE: { Val c = eval(b, n.a[0]); if (!c.known()) { Val x = eval(b, n.a[1]); Val y = eval(b, n.a[2]);
                Val r = Val::U(x.sz); for (int i = 0; i < x.sz; i++) if (((x.kn >> i) & 1) && ((y.kn >> i) & 1) && x.byte(i) == y.byte(i)) r.set_byte(i, x.byte(i), true); return r; }
            return c.u64() ? eval(b, n.a[1]) : eval(b, n.a[2]); }
        case IRNodeC::LOAD: { Val a = eval(b, n.a[0]); emit_acc(a, n.size, 0); if (!a.known()) return Val::U(n.size); return shadow_load(a.u64(), n.size); }
        case IRNodeC::CCALL: return ccall(b, n);
        case IRNodeC::TRIOP: case IRNodeC::QOP: case IRNodeC::GETI: default: n_unknown_ops++; return Val::U(n.k == IRNodeC::GETI ? n.size : 8);
    }
}

bool VexInterp::exec(const IRBlockC& b, const std::function<void(const MemAccess&)>& emit) {
    if (!b.ok) return false;
    emit_ = &emit;
    if (t_.size() < b.tmpty.size()) t_.resize(b.tmpty.size());
    for (size_t i = 0; i < b.tmpty.size(); i++) { t_[i].sz = ty_size(b.tmpty[i]); t_[i].kn = 0; }
    for (const IRStmtC& s : b.stmts) {
        switch (s.k) {
            case IRStmtC::PUT: { Val v = eval(b, s.e[0]); int sz = s.size < v.sz ? s.size : v.sz;
                for (int i = 0; i < sz && s.off + i < 1024; i++) { g[s.off + i] = v.byte(i); gk[s.off + i] = (v.kn >> i) & 1; } break; }
            case IRStmtC::PUTI: set_reg_unknown(s.off, s.size); break;
            case IRStmtC::WRTMP: t_[s.tmp] = eval(b, s.e[0]); break;
            case IRStmtC::STORE: { Val a = eval(b, s.e[0]); Val d = eval(b, s.e[1]); emit_acc(a, s.size, 1); if (a.known()) shadow_store(a.u64(), d, s.size); break; }
            case IRStmtC::STOREG: { Val gd = eval(b, s.e[2]); if (gd.known() && !gd.u64()) break; Val a = eval(b, s.e[0]); Val d = eval(b, s.e[1]);
                if (!gd.known()) { Val ua = Val::U(8); emit_acc(ua, s.size, 1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); break; }
                emit_acc(a, s.size, 1); if (a.known()) shadow_store(a.u64(), d, s.size); break; }
            case IRStmtC::LOADG: { Val gd = eval(b, s.e[2]); int dsz = ty_size(b.tmpty[s.tmp]);
                if (gd.known() && !gd.u64()) { t_[s.tmp] = eval(b, s.e[1]); break; }
                Val a = eval(b, s.e[0]);
                if (!gd.known()) { Val ua = Val::U(8); emit_acc(ua, s.size, 0); t_[s.tmp] = Val::U(dsz); break; }
                emit_acc(a, s.size, 0);
                Val v = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size);
                switch (s.cvt) { case ILGop_16Sto32: case ILGop_8Sto32: v = widen(v, 4, true); break; case ILGop_16Uto32: case ILGop_8Uto32: v = widen(v, 4, false); break; default: break; }
                v.sz = dsz; t_[s.tmp] = v; break; }
            case IRStmtC::CAS: { Val a = eval(b, s.e[0]); emit_acc(a, s.size, 2);
                int esz = s.tmp2 >= 0 ? s.size / 2 : s.size;
                t_[s.tmp] = a.known() ? shadow_load(a.u64(), esz) : Val::U(esz);
                if (s.tmp2 >= 0) t_[s.tmp2] = a.known() ? shadow_load(a.u64() + esz, esz) : Val::U(esz);
                if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size);   // outcome unknown -> slot unknown
                break; }
            case IRStmtC::LLSC: { Val a = eval(b, s.e[0]);
                if (s.e[1] >= 0) { emit_acc(a, s.size, 1); t_[s.tmp] = Val::U(1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); }
                else { emit_acc(a, s.size, 0); t_[s.tmp] = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size); }
                break; }
            case IRStmtC::DIRTY: { if (s.tmp >= 0) t_[s.tmp] = Val::U(ty_size(b.tmpty[s.tmp]));
                if (s.mfx != Ifx_None && s.e[1] >= 0) { Val a = eval(b, s.e[1]); int op = s.mfx == Ifx_Read ? 0 : s.mfx == Ifx_Write ? 1 : 2; emit_acc(a, s.msize, op);
                    if (a.known() && s.mfx != Ifx_Read) shadow_store(a.u64(), Val::U(s.msize), s.msize); }
                break; }
            default: break;   // NOP, IMARK, MBE, EXIT (control flow comes from PT)
        }
    }
    emit_ = nullptr;
    return true;
}
