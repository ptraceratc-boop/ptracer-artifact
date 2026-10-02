// vexinterp.cpp -- see vexinterp.h. Known/unknown-lattice evaluation of VEX IR for address
// reconstruction. Anything not needed for address arithmetic degrades to "unknown" (never wrong).
#include "vexinterp.h"
#include <cstring>
#include <cstdlib>
#include <climits>
#include <new>
#include <emmintrin.h>
#include <libvex_guest_offsets.h>

// Eight known-bits at a time.  `gk' holds one 0/1 byte per guest byte, so the byte-at-a-time
// loop this replaces cost 17 % of the whole reconstruction on the PolyBench probe cell:
// every GET of a register and every effective-address computation goes through it.
// The 8-byte groups are read with one load each; the remainder keeps the byte loop.  Bit-for-bit identical to the loop by construction: `_mm_cmpgt_epi8' against zero
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
// Known-bit byte expansion: bit i of `m' -> byte i = 0/1 (the `gk' guest-state mask format).
static inline uint64_t kn_expand8(uint32_t m) {
    uint64_t x = m & 0xffu;
    x = (x | (x << 28)) & 0x0000000F0000000Full;
    x = (x | (x << 14)) & 0x0003000300030003ull;
    x = (x | (x << 7))  & 0x0101010101010101ull;
    return x;
}
static inline int ty_size(IRType t) { return t == Ity_I1 ? 1 : sizeofIRType(t); }

// The x86-64 psABI requires DF = 0 at every function boundary, and every compiler and libc keeps
// it that way (`std' is only ever used in a `std'/`cld' pair, whose writes VEX models and this
// interpreter therefore sees).  VEX represents it as `guest_DFLAG' = +1 (DF clear) or -1, and it
// is what a string instruction adds to %rdi/%rsi after every element -- so leaving it UNKNOWN
// makes the SECOND and every later iteration of an expanded `rep stos'/`rep movs' compute an
// unknown address, even when %rdi, %rsi and %rcx are all known (measured on
// whole-program CPython start-up: 312 728 of the 312 760 records of one `rep stosb' in
// __memset_avx2_unaligned_erms).  It is therefore initialised, and restored after a state loss,
// exactly like the TLS base.
void VexInterp::init_invariants() { set_reg(OFFSET_amd64_DFLAG, 8, 1); }
VexInterp::VexInterp() { memset(g, 0, sizeof g); memset(gk, 0, sizeof gk); init_invariants(); tree_ = tree_mode(); }
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
    size_t nargs = n.cargs.size() > 8 ? 8 : n.cargs.size();
    // Only the three flag helpers below can produce a known result, each with 4 or 5 args; for any
    // other call whose args are side-effect-free atoms the outcome is the fall-through `unknown'
    // without evaluating them (identical result and counters).
    if (n.cargs_atomic && (n.ccid == 0 || nargs > 5)) { n_ccall_unknown++; return Val::U(rsz); }
    Val argbuf[8]; for (size_t i = 0; i < nargs; i++) argbuf[i] = eval(b, n.cargs[i]);
    return ccall_args(n, argbuf, nargs);
}
Val VexInterp::ccall_args(const IRNodeC& n, Val* argbuf, size_t nargs) {
    int rsz = ty_size(n.ty);
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
    if (n.ccid == 1 && args.size() == 5 && args[0].known()) {
        uint64_t cond = args[0].u64();
        if (!flags_of(1, &cf, &pf, &zf, &sf, &of)) { n_ccall_unknown++; return Val::U(rsz); }
        int r;
        switch (cond >> 1) { case 0: r = of; break; case 1: r = cf; break; case 2: r = zf; break; case 3: r = cf | zf; break; case 4: r = sf; break; case 5: r = pf; break; case 6: r = sf ^ of; break; case 7: r = (sf ^ of) | zf; break; default: r = 0; }
        if (cond & 1) r = !r;
        n_ccall_known++; return Val::K(r, rsz);
    }
    if (n.ccid == 2 && args.size() == 4) {
        if (!flags_of(0, &cf, &pf, &zf, &sf, &of)) { n_ccall_unknown++; return Val::U(rsz); }
        n_ccall_known++; return Val::K(cf, rsz);
    }
    if (n.ccid == 3 && args.size() == 4) {
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
    emit_ = &emit; use_sink_ = false;
    bool r = exec_tree(b);
    emit_ = nullptr;
    return r;
}

bool VexInterp::exec_tree(const IRBlockC& b) {
    if (t_.size() < b.tmpty.size()) t_.resize(b.tmpty.size());
    if (b.tmpsz.size() == b.tmpty.size()) { for (size_t i = 0; i < b.tmpsz.size(); i++) { t_[i].sz = b.tmpsz[i]; t_[i].kn = 0; } }
    else for (size_t i = 0; i < b.tmpty.size(); i++) { t_[i].sz = ty_size(b.tmpty[i]); t_[i].kn = 0; }
    for (const IRStmtC& s : b.stmts) {
        switch (s.k) {
            case IRStmtC::PUT: { Val v = eval(b, s.e[0]); int sz = s.size < v.sz ? s.size : v.sz;
                // Same bytes as a per-byte loop (value bytes are w[] in little-endian order;
                // known bit i -> known byte 0/1), written 8 at a time; a per-byte loop is ~35 % of exec().
                if (s.off < 0) break;
                if (sz > 1024 - s.off) sz = 1024 - s.off;
                if (sz > 32) sz = 32;
                if (sz > 0) { memcpy(g + s.off, v.w, (size_t)sz);
                    for (int j = 0; j < sz; j += 8) { uint64_t e = kn_expand8((v.kn >> j) & 0xffu); memcpy(gk + s.off + j, &e, (size_t)(sz - j < 8 ? sz - j : 8)); } }
                break; }
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
    return true;
}
/* ---------------- the compiled skeleton --------------------------------------------------------
 * exec_tree() walks every statement's expression tree on every execution.  compile() turns a
 * lifted instruction once into a flat op list over value slots and, on the way, removes what the
 * reconstruction can never observe.  Two rules, both exact (the machine state after the block --
 * every KNOWN byte of the guest state and of the shadow memory -- and the sequence of emitted
 * accesses are those of exec_tree(); only the diagnostic counters unknown_ops/ccall_* and the
 * don't-care contents of UNKNOWN bytes may differ):
 *  (1) FOLD: an expression that is unknown by construction -- whatever the state -- is not
 *      evaluated.  "By construction" is decided by asking the evaluator itself with all-unknown
 *      inputs (unop/binop are pure functions of (op, input values), and the known mask of their
 *      result never depends on the contents of unknown input bytes), plus two closed rules: an op
 *      the evaluator does not implement returns unknown for any input (detected as "unknown even
 *      for fully known non-zero inputs"), and the STRICT ops (listed below, the `both() ? .. : U'
 *      cases) are unknown as soon as one input is.  Sizes are static except behind an ITE with
 *      branches of different sizes, which is never folded.  PUT of a folded value = mark those
 *      guest bytes unknown; STORE of one = forget those shadow bytes (exactly what shadow_store
 *      does with an all-unknown value, in and outside the stack window).
 *  (2) DEAD: backward liveness inside the block.  The roots are the statements with an effect the
 *      reconstructor sees: PUT (guest state), STORE/STOREG/CAS/LLSC/DIRTY (emits + shadow), and
 *      every LOAD (its emit).  A temp no root reads is not computed; a LOAD whose value is dead is
 *      reduced to its address + emit.  EXIT guards are dead by definition (exec_tree never
 *      evaluated them: control flow comes from PT) -- this removes the flag computation of every
 *      conditional branch, which is exactly the paper's "the skeleton excludes the instructions
 *      that compute the control conditions".
 * Eval order: expressions are compiled at their statement, in exec_tree's post-order; the only
 * side effect an expression has is a LOAD's emit, and a LOAD anywhere but at the top of a WRTMP
 * (never produced by VEX's flat IR) sends the whole block back to exec_tree. */
namespace {
enum : uint8_t { OP_GET, OP_GET8, OP_UNOP, OP_BINOP, OP_ADD64, OP_ITE, OP_CCALL, OP_LOAD, OP_LOADE, OP_PUT, OP_PUT8, OP_PUTU,
                 OP_PUTI, OP_STORE, OP_STOREU, OP_STOREG, OP_LOADG, OP_CAS, OP_LLSC, OP_DIRTY, OP_MARK };
bool binop_strict(IROp op) {
    switch (op) {
        case Iop_Add8: case Iop_Add16: case Iop_Add32: case Iop_Add64: case Iop_Sub8: case Iop_Sub16: case Iop_Sub32: case Iop_Sub64:
        case Iop_Mul8: case Iop_Mul16: case Iop_Mul32: case Iop_Mul64: case Iop_Shl8: case Iop_Shl16: case Iop_Shl32: case Iop_Shl64:
        case Iop_Shr8: case Iop_Shr16: case Iop_Shr32: case Iop_Shr64: case Iop_Sar8: case Iop_Sar16: case Iop_Sar32: case Iop_Sar64:
        case Iop_CmpEQ8: case Iop_CmpEQ16: case Iop_CmpEQ32: case Iop_CmpEQ64: case Iop_CasCmpEQ8: case Iop_CasCmpEQ16: case Iop_CasCmpEQ32: case Iop_CasCmpEQ64:
        case Iop_CmpNE8: case Iop_CmpNE16: case Iop_CmpNE32: case Iop_CmpNE64: case Iop_CasCmpNE8: case Iop_CasCmpNE16: case Iop_CasCmpNE32: case Iop_CasCmpNE64:
        case Iop_ExpCmpNE8: case Iop_ExpCmpNE16: case Iop_ExpCmpNE32: case Iop_ExpCmpNE64:
        case Iop_CmpLT32S: case Iop_CmpLT64S: case Iop_CmpLE32S: case Iop_CmpLE64S: case Iop_CmpLT32U: case Iop_CmpLT64U: case Iop_CmpLE32U: case Iop_CmpLE64U:
        case Iop_MullU8: case Iop_MullU16: case Iop_MullU32: case Iop_MullS8: case Iop_MullS16: case Iop_MullS32: case Iop_MullU64: case Iop_MullS64:
        case Iop_DivModU64to32: case Iop_DivModS64to32: case Iop_DivModU128to64: case Iop_DivModS128to64: case Iop_Max32U:
            return true;
        default: return false;
    }
}
struct Info { int sz = -1; bool au = false; };
}  // namespace

void CProgFree::operator()(CProg* p) const { if (p) { p->~CProg(); free(p); } }

bool VexInterp::tree_mode() { static int m = -1; if (m < 0) { const char* e = getenv("PTRECON_INTERP"); m = (e && !strcmp(e, "tree")) ? 1 : 0; } return m == 1; }

void VexInterp::compile(const IRBlockC& b) {
    struct Build { bool fallback = false; std::vector<COp> ops; std::vector<Val> consts; std::vector<int32_t> args; int n_dead = 0, n_folded = 0; };
    std::unique_ptr<Build> P(new Build);
    struct Freeze { const IRBlockC& b; std::unique_ptr<Build>& P; int nslots = 0;
        ~Freeze() {   // flatten into one allocation (header, ops, consts, args), whatever path returned
            size_t h = (sizeof(CProg) + 63) & ~(size_t)63, o = P->ops.size() * sizeof(COp), c = P->consts.size() * sizeof(Val), a = P->args.size() * sizeof(int32_t);
            size_t tot = (h + o + c + a + 63) & ~(size_t)63;
            uint8_t* m = (uint8_t*)aligned_alloc(64, tot);
            CProg* q = new (m) CProg();
            q->fallback = P->fallback; q->nslots = nslots; q->nops = (int)P->ops.size(); q->nconsts = (int)P->consts.size(); q->nargs = (int)P->args.size();
            q->n_dead = P->n_dead; q->n_folded = P->n_folded;
            COp* op = (COp*)(m + h); for (size_t i = 0; i < P->ops.size(); i++) new (op + i) COp(P->ops[i]);
            Val* cv = (Val*)(m + h + o); for (size_t i = 0; i < P->consts.size(); i++) new (cv + i) Val(P->consts[i]);
            int32_t* av = (int32_t*)(m + h + o + c); for (size_t i = 0; i < P->args.size(); i++) av[i] = P->args[i];
            q->ops = op; q->consts = cv; q->args = av;
            b.prog.reset(q);
        } } fz{b, P};
    const int N = (int)b.nodes.size(), T = (int)b.tmpty.size();
    const uint64_t save_u = n_unknown_ops, save_ck = n_ccall_known, save_cu = n_ccall_unknown;   // probes below count
    // ---- shape check: a LOAD only at the top of a WRTMP, never nested -----------------------
    std::vector<uint8_t> top_load(N, 0);
    for (const IRStmtC& s : b.stmts) if (s.k == IRStmtC::WRTMP && s.e[0] >= 0 && b.nodes[s.e[0]].k == IRNodeC::LOAD) top_load[s.e[0]] = 1;
    for (int i = 0; i < N; i++) if (b.nodes[i].k == IRNodeC::LOAD && !top_load[i]) { P->fallback = true; n_prog_fallback++; return; }
    // ---- static size / always-unknown per node, temps in statement order --------------------
    std::vector<Info> ni(N), ti(T);
    std::vector<uint8_t> twritten(T, 0), tdef_wrtmp(T, 0);
    for (const IRStmtC& s : b.stmts) {
        if (s.tmp >= 0 && s.tmp < T && (s.k == IRStmtC::WRTMP || s.k == IRStmtC::LOADG || s.k == IRStmtC::CAS || s.k == IRStmtC::LLSC || s.k == IRStmtC::DIRTY)) twritten[s.tmp] = 1;
        if (s.k == IRStmtC::CAS && s.tmp2 >= 0 && s.tmp2 < T) twritten[s.tmp2] = 1;
    }
    std::vector<uint8_t> done(N, 0);
    std::function<Info(int)> info = [&](int x) -> Info {
        if (x < 0 || x >= N) return Info{};
        if (done[x]) return ni[x];
        const IRNodeC& n = b.nodes[x]; Info r;
        switch (n.k) {
            case IRNodeC::CONST: r.sz = n.size; break;
            case IRNodeC::GET: r.sz = n.size; r.au = n.off < 0 || n.off + n.size > 1024; break;
            case IRNodeC::GETI: r.sz = n.size; r.au = true; break;
            case IRNodeC::RDTMP: { int t = n.off; if (t < 0 || t >= T) { r.sz = -1; break; }
                if (!twritten[t]) { r.sz = b.tmpsz.size() == (size_t)T ? b.tmpsz[t] : ty_size(b.tmpty[t]); r.au = true; }
                else if (tdef_wrtmp[t]) r = ti[t];
                else r.sz = -1;          // LOADG/CAS/LLSC/DIRTY temp: size from the statement, never folded
                break; }
            case IRNodeC::UNOP: { Info a = info(n.a[0]); if (a.sz < 0) break;
                Val u = unop(n.op, Val::U(a.sz)); r.sz = u.sz;
                if (a.au) r.au = u.kn == 0;
                else { Val k = unop(n.op, Val::K(0x0123456789abcdefull, a.sz)); r.au = k.kn == 0; }
                break; }
            case IRNodeC::BINOP: { Info a = info(n.a[0]), c = info(n.a[1]); if (a.sz < 0 || c.sz < 0) break;
                Val u = binop(n.op, Val::U(a.sz), Val::U(c.sz)); r.sz = u.sz;
                if (a.au && c.au) r.au = u.kn == 0;
                else if ((a.au || c.au) && binop_strict(n.op)) r.au = true;
                else { Val k = binop(n.op, Val::K(0x0123456789abcdefull, a.sz), Val::K(3, c.sz)); r.au = k.kn == 0; }
                break; }
            case IRNodeC::ITE: { Info c = info(n.a[0]), x2 = info(n.a[1]), y = info(n.a[2]); (void)c;
                if (x2.sz < 0 || y.sz < 0 || x2.sz != y.sz) break;
                r.sz = x2.sz; r.au = x2.au && y.au; break; }
            case IRNodeC::LOAD: info(n.a[0]); r.sz = n.size; break;
            case IRNodeC::CCALL: { for (int a : n.cargs) info(a);
                size_t na = n.cargs.size() > 8 ? 8 : n.cargs.size();
                r.sz = ty_size(n.ty);
                r.au = !((n.ccid == 1 && na == 5) || ((n.ccid == 2 || n.ccid == 3) && na == 4));
                break; }
            default: r.sz = 8; r.au = true; break;   // TRIOP, QOP, UNK: eval() returns U(8)
        }
        done[x] = 1; ni[x] = r; return r;
    };
    for (const IRStmtC& s : b.stmts) {
        for (int e : s.e) if (e >= 0) info(e);
        if (s.k == IRStmtC::WRTMP && s.tmp >= 0 && s.tmp < T) { ti[s.tmp] = info(s.e[0]); tdef_wrtmp[s.tmp] = 1; }
    }
    n_unknown_ops = save_u; n_ccall_known = save_ck; n_ccall_unknown = save_cu;
    // Note: `info' of a RDTMP is memoised at its first use, which follows its WRTMP (SSA), so the
    // temp's Info is final by then.
    auto folded = [&](int x) { return x >= 0 && x < N && ni[x].sz >= 0 && ni[x].au; };
    // ---- backward liveness ------------------------------------------------------------------
    std::vector<uint8_t> tlive(T, 0);
    std::function<void(int)> mark = [&](int x) {
        if (x < 0 || x >= N || folded(x)) return;
        const IRNodeC& n = b.nodes[x];
        switch (n.k) {
            case IRNodeC::RDTMP: if (n.off >= 0 && n.off < T) tlive[n.off] = 1; break;
            case IRNodeC::UNOP: case IRNodeC::BINOP: case IRNodeC::ITE: case IRNodeC::LOAD:
                for (int a : n.a) if (a >= 0) mark(a); break;
            case IRNodeC::CCALL: for (int a : n.cargs) mark(a); break;
            default: break;
        }
    };
    const int S = (int)b.stmts.size();
    std::vector<uint8_t> keep(S, 0);   // 0 drop, 1 keep, 2 LOAD emit only, 3 PUT/STORE of a folded value
    for (int i = S - 1; i >= 0; i--) {
        const IRStmtC& s = b.stmts[i];
        switch (s.k) {
            case IRStmtC::PUT: if (folded(s.e[0])) keep[i] = 3; else { keep[i] = 1; mark(s.e[0]); } break;
            case IRStmtC::PUTI: keep[i] = 1; break;
            case IRStmtC::WRTMP: {
                bool isload = s.e[0] >= 0 && b.nodes[s.e[0]].k == IRNodeC::LOAD;
                if (s.tmp >= 0 && s.tmp < T && tlive[s.tmp]) { keep[i] = 1; mark(s.e[0]); }
                else if (isload) { keep[i] = 2; mark(b.nodes[s.e[0]].a[0]); }
                break; }
            case IRStmtC::STORE: keep[i] = 1; mark(s.e[0]); if (folded(s.e[1])) keep[i] = 3; else mark(s.e[1]); break;
            case IRStmtC::STOREG: keep[i] = 1; mark(s.e[2]); mark(s.e[0]); mark(s.e[1]); break;
            case IRStmtC::LOADG: keep[i] = 1; mark(s.e[2]); mark(s.e[1]); mark(s.e[0]); break;
            case IRStmtC::CAS: case IRStmtC::LLSC: keep[i] = 1; mark(s.e[0]); break;
            case IRStmtC::DIRTY: keep[i] = 1; if (s.mfx != Ifx_None && s.e[1] >= 0) mark(s.e[1]); break;
            default: break;   // NOP, IMARK, MBE, EXIT
        }
    }
    // ---- emit the op list ---------------------------------------------------------------------
    std::vector<int32_t> talias(T, INT32_MIN);
    auto konst = [&](const Val& v) { P->consts.push_back(v); return -(int32_t)P->consts.size(); };
    std::function<int32_t(int, int32_t)> cx = [&](int x, int32_t dst) -> int32_t {
        if (x < 0 || x >= N) return konst(Val::U(8));
        const IRNodeC& n = b.nodes[x];
        if (folded(x)) { if (n.k != IRNodeC::RDTMP && n.k != IRNodeC::CONST) P->n_folded++; return konst(Val::U(ni[x].sz)); }
        if (dst < 0) dst = T + x;
        COp o; o.dst = dst;
        switch (n.k) {
            case IRNodeC::CONST: { Val r = Val::U(n.size); r.kn = Val::mask(n.size); for (int i = 0; i < 4; i++) r.w[i] = n.c[i]; return konst(r); }
            case IRNodeC::RDTMP: { int t = n.off; if (t < 0 || t >= T) return konst(Val::U(8)); if (talias[t] != INT32_MIN) return talias[t]; return t; }
            case IRNodeC::GET: o.k = n.size == 8 ? OP_GET8 : OP_GET; o.off = n.off; o.sz = (uint8_t)n.size; break;
            case IRNodeC::UNOP: o.a = cx(n.a[0], -1); o.k = OP_UNOP; o.op = n.op; break;
            case IRNodeC::BINOP: o.a = cx(n.a[0], -1); o.b = cx(n.a[1], -1); o.k = n.op == Iop_Add64 ? OP_ADD64 : OP_BINOP; o.op = n.op; break;
            case IRNodeC::ITE: o.a = cx(n.a[0], -1); o.b = cx(n.a[1], -1); o.c = cx(n.a[2], -1); o.k = OP_ITE; break;
            case IRNodeC::CCALL: { std::vector<int32_t> av; size_t na = n.cargs.size() > 8 ? 8 : n.cargs.size();
                for (size_t i = 0; i < na; i++) av.push_back(cx(n.cargs[i], -1));
                o.k = OP_CCALL; o.off = (int32_t)P->args.size(); o.b = (int32_t)na; o.p = &n;
                for (int32_t v : av) P->args.push_back(v); break; }
            default: return konst(Val::U(8));     // not reached: LOAD is a statement, the rest fold
        }
        P->ops.push_back(o); return dst;
    };
    for (int i = 0; i < S; i++) {
        const IRStmtC& s = b.stmts[i];
        if (!keep[i]) { if (s.k == IRStmtC::WRTMP || s.k == IRStmtC::PUT) P->n_dead++; continue; }
        COp o; o.p = &s;
        switch (s.k) {
            case IRStmtC::PUT:
                if (keep[i] == 3) { int vs = ni[s.e[0]].sz; int sz = s.size < vs ? s.size : vs;
                    if (s.off < 0) continue; if (sz > 1024 - s.off) sz = 1024 - s.off; if (sz > 32) sz = 32; if (sz <= 0) continue;
                    o.k = OP_PUTU; o.off = s.off; o.sz = (uint8_t)sz; P->n_folded++; }
                else { o.a = cx(s.e[0], -1); o.off = s.off; o.b = s.size; o.k = (s.size == 8 && s.off >= 0 && s.off + 8 <= 1024 && ni[s.e[0]].sz == 8) ? OP_PUT8 : OP_PUT; }
                break;
            case IRStmtC::PUTI: o.k = OP_PUTI; o.off = s.off; o.b = s.size; break;
            case IRStmtC::WRTMP: {
                const IRNodeC& n = b.nodes[s.e[0]];
                if (n.k == IRNodeC::LOAD) { o.a = cx(n.a[0], -1); o.sz = (uint8_t)n.size;
                    if (keep[i] == 1) { o.k = OP_LOAD; o.dst = s.tmp; talias[s.tmp] = s.tmp; } else o.k = OP_LOADE;
                    break; }
                talias[s.tmp] = cx(s.e[0], s.tmp); continue; }
            case IRStmtC::STORE: o.a = cx(s.e[0], -1); o.b = s.size;
                if (keep[i] == 3) { o.k = OP_STOREU; P->n_folded++; } else { o.c = cx(s.e[1], -1); o.k = OP_STORE; } break;
            case IRStmtC::STOREG: o.c = cx(s.e[2], -1); o.a = cx(s.e[0], -1); o.b = cx(s.e[1], -1); o.k = OP_STOREG; break;
            case IRStmtC::LOADG: o.c = cx(s.e[2], -1); o.b = cx(s.e[1], -1); o.a = cx(s.e[0], -1); o.k = OP_LOADG; o.off = ty_size(b.tmpty[s.tmp]);
                talias[s.tmp] = s.tmp; break;
            case IRStmtC::CAS: o.a = cx(s.e[0], -1); o.k = OP_CAS; if (s.tmp >= 0) talias[s.tmp] = s.tmp; if (s.tmp2 >= 0) talias[s.tmp2] = s.tmp2; break;
            case IRStmtC::LLSC: o.a = cx(s.e[0], -1); o.k = OP_LLSC; if (s.tmp >= 0) talias[s.tmp] = s.tmp; break;
            case IRStmtC::DIRTY: o.a = (s.mfx != Ifx_None && s.e[1] >= 0) ? cx(s.e[1], -1) : 0; o.k = OP_DIRTY;
                if (s.tmp >= 0) { talias[s.tmp] = s.tmp; o.off = ty_size(b.tmpty[s.tmp]); } break;
            default: continue;
        }
        P->ops.push_back(o);
    }
    fz.nslots = T + N;
    n_prog++; n_stmt_dead += P->n_dead; n_expr_folded += P->n_folded;
}

void VexInterp::run_prog(const CProg& P) {
    Val* S = t_.data();
    const Val* C = P.consts;
    auto V = [&](int32_t i) -> const Val& { return i >= 0 ? S[i] : C[-1 - i]; };
    for (const COp* op = P.ops, *oe = P.ops + P.nops; op != oe; ++op) { const COp& o = *op;
        switch (o.k) {
            case OP_GET8: { Val& r = S[o.dst]; memcpy(&r.w[0], g + o.off, 8); r.kn = kn_mask8(gk + o.off); r.sz = 8; break; }
            case OP_GET: S[o.dst] = get_reg(o.off, o.sz); break;
            case OP_UNOP: S[o.dst] = unop((IROp)o.op, V(o.a)); break;
            case OP_ADD64: { const Val& a = V(o.a); const Val& c = V(o.b);
                if (a.sz == 8 && c.sz == 8) { Val& r = S[o.dst]; r.sz = 8;
                    if (a.kn == 0xff && c.kn == 0xff) { r.w[0] = a.w[0] + c.w[0]; r.kn = 0xff; } else { r.w[0] = 0; r.kn = 0; }
                    r.w[1] = r.w[2] = r.w[3] = 0; break; }
                S[o.dst] = binop(Iop_Add64, a, c); break; }
            case OP_BINOP: S[o.dst] = binop((IROp)o.op, V(o.a), V(o.b)); break;
            case OP_ITE: { const Val& c = V(o.a);
                if (!c.known()) { const Val& x = V(o.b); const Val& y = V(o.c);
                    Val r = Val::U(x.sz); for (int i = 0; i < x.sz; i++) if (((x.kn >> i) & 1) && ((y.kn >> i) & 1) && x.byte(i) == y.byte(i)) r.set_byte(i, x.byte(i), true);
                    S[o.dst] = r; }
                else S[o.dst] = c.u64() ? V(o.b) : V(o.c);
                break; }
            case OP_CCALL: { Val argbuf[8]; for (int i = 0; i < o.b; i++) argbuf[i] = V(P.args[o.off + i]);
                S[o.dst] = ccall_args(*(const IRNodeC*)o.p, argbuf, (size_t)o.b); break; }
            case OP_LOAD: { const Val& a = V(o.a); emit_acc(a, o.sz, 0); S[o.dst] = a.known() ? shadow_load(a.u64(), o.sz) : Val::U(o.sz); break; }
            case OP_LOADE: emit_acc(V(o.a), o.sz, 0); break;
            case OP_PUT8: { const Val& v = V(o.a); if (v.sz != 8) goto generic_put;
                memcpy(g + o.off, &v.w[0], 8); uint64_t e = kn_expand8(v.kn & 0xffu); memcpy(gk + o.off, &e, 8); break; }
            generic_put:
            case OP_PUT: { const Val& v = V(o.a); int sz = o.b < v.sz ? o.b : v.sz;
                if (o.off < 0) break;
                if (sz > 1024 - o.off) sz = 1024 - o.off;
                if (sz > 32) sz = 32;
                if (sz > 0) { memcpy(g + o.off, v.w, (size_t)sz);
                    for (int j = 0; j < sz; j += 8) { uint64_t e = kn_expand8((v.kn >> j) & 0xffu); memcpy(gk + o.off + j, &e, (size_t)(sz - j < 8 ? sz - j : 8)); } }
                break; }
            case OP_PUTU: switch (o.sz) {   // constant sizes: inlined stores instead of two memset calls
                    case 8: memset(g + o.off, 0, 8); memset(gk + o.off, 0, 8); break;
                    case 16: memset(g + o.off, 0, 16); memset(gk + o.off, 0, 16); break;
                    case 32: memset(g + o.off, 0, 32); memset(gk + o.off, 0, 32); break;
                    default: memset(g + o.off, 0, o.sz); memset(gk + o.off, 0, o.sz); break; }
                break;
            case OP_PUTI: set_reg_unknown(o.off, o.b); break;
            case OP_STORE: { const Val& a = V(o.a); emit_acc(a, o.b, 1); if (a.known()) shadow_store(a.u64(), V(o.c), o.b); break; }
            case OP_STOREU: { const Val& a = V(o.a); emit_acc(a, o.b, 1); if (a.known()) shadow_forget(a.u64(), o.b); break; }
            case OP_STOREG: { const IRStmtC& s = *(const IRStmtC*)o.p; const Val& gd = V(o.c); if (gd.known() && !gd.u64()) break;
                const Val& a = V(o.a);
                if (!gd.known()) { Val ua = Val::U(8); emit_acc(ua, s.size, 1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); break; }
                emit_acc(a, s.size, 1); if (a.known()) shadow_store(a.u64(), V(o.b), s.size); break; }
            case OP_LOADG: { const IRStmtC& s = *(const IRStmtC*)o.p; const Val& gd = V(o.c); int dsz = o.off;
                if (gd.known() && !gd.u64()) { S[s.tmp] = V(o.b); break; }
                const Val& a = V(o.a);
                if (!gd.known()) { Val ua = Val::U(8); emit_acc(ua, s.size, 0); S[s.tmp] = Val::U(dsz); break; }
                emit_acc(a, s.size, 0);
                Val v = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size);
                switch (s.cvt) { case ILGop_16Sto32: case ILGop_8Sto32: v = widen(v, 4, true); break; case ILGop_16Uto32: case ILGop_8Uto32: v = widen(v, 4, false); break; default: break; }
                v.sz = dsz; S[s.tmp] = v; break; }
            case OP_CAS: { const IRStmtC& s = *(const IRStmtC*)o.p; Val a = V(o.a); emit_acc(a, s.size, 2);
                int esz = s.tmp2 >= 0 ? s.size / 2 : s.size;
                S[s.tmp] = a.known() ? shadow_load(a.u64(), esz) : Val::U(esz);
                if (s.tmp2 >= 0) S[s.tmp2] = a.known() ? shadow_load(a.u64() + esz, esz) : Val::U(esz);
                if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size);
                break; }
            case OP_LLSC: { const IRStmtC& s = *(const IRStmtC*)o.p; Val a = V(o.a);
                if (s.e[1] >= 0) { emit_acc(a, s.size, 1); S[s.tmp] = Val::U(1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); }
                else { emit_acc(a, s.size, 0); S[s.tmp] = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size); }
                break; }
            case OP_DIRTY: { const IRStmtC& s = *(const IRStmtC*)o.p; if (s.tmp >= 0) S[s.tmp] = Val::U(o.off);
                if (s.mfx != Ifx_None && s.e[1] >= 0) { Val a = V(o.a); int op = s.mfx == Ifx_Read ? 0 : s.mfx == Ifx_Write ? 1 : 2; emit_acc(a, s.msize, op);
                    if (a.known() && s.mfx != Ifx_Read) shadow_store(a.u64(), Val::U(s.msize), s.msize); }
                break; }
        }
    }
}

bool VexInterp::build_fused(const IRBlockC* const* blks, int n, FProg& F) {
    F.ops.clear(); F.args.clear(); F.slots.clear(); F.ninsn = n; F.ops_in = 0;
    int base = 0;
    for (int i = 0; i < n; i++) {
        const IRBlockC& b = *blks[i];
        if (!b.ok) return false;
        if (!b.prog) compile(b);
        const CProg& P = *b.prog;
        if (P.fallback) return false;
        const int cb = base + P.nslots;
        F.slots.resize((size_t)(cb + P.nconsts));
        for (int c = 0; c < P.nconsts; c++) F.slots[(size_t)(cb + c)] = P.consts[c];
        auto M = [&](int32_t x) -> int32_t { return x >= 0 ? base + x : cb + (-1 - x); };
        COp mk; mk.k = OP_MARK; F.ops.push_back(mk);
        for (int j = 0; j < P.nops; j++) {
            COp o = P.ops[j]; F.ops_in++;
            switch (o.k) {
                case OP_GET: case OP_GET8: o.dst = M(o.dst); break;
                case OP_UNOP: o.dst = M(o.dst); o.a = M(o.a); break;
                case OP_BINOP: case OP_ADD64: o.dst = M(o.dst); o.a = M(o.a); o.b = M(o.b); break;
                case OP_ITE: o.dst = M(o.dst); o.a = M(o.a); o.b = M(o.b); o.c = M(o.c); break;
                case OP_CCALL: { o.dst = M(o.dst); int32_t at = (int32_t)F.args.size();
                    for (int a = 0; a < o.b; a++) F.args.push_back(M(P.args[o.off + a])); o.off = at; break; }
                case OP_LOAD: o.dst = M(o.dst); o.a = M(o.a); break;
                case OP_LOADE: case OP_STOREU: o.a = M(o.a); break;
                case OP_PUT: case OP_PUT8: o.a = M(o.a); break;
                case OP_PUTU: case OP_PUTI: break;
                case OP_STORE: o.a = M(o.a); o.c = M(o.c); break;
                case OP_STOREG: o.a = M(o.a); o.b = M(o.b); o.c = M(o.c); break;
                case OP_LOADG: { const IRStmtC& st = *(const IRStmtC*)o.p; o.a = M(o.a); o.b = M(o.b); o.c = M(o.c); o.dst = M(st.tmp); break; }
                case OP_CAS: { const IRStmtC& st = *(const IRStmtC*)o.p; o.a = M(o.a); o.dst = M(st.tmp); o.b = st.tmp2 >= 0 ? M(st.tmp2) : -1; break; }
                case OP_LLSC: { const IRStmtC& st = *(const IRStmtC*)o.p; o.a = M(o.a); o.dst = M(st.tmp); break; }
                case OP_DIRTY: { const IRStmtC& st = *(const IRStmtC*)o.p; o.a = M(o.a); o.dst = st.tmp >= 0 ? M(st.tmp) : -1; break; }
                default: return false;
            }
            F.ops.push_back(o);
        }
        base = cb + P.nconsts;
    }
    // ---- backward pass: guest bytes overwritten later in the run (dead) + slot liveness ----------
    std::vector<uint8_t> dead(1024, 0), live(F.slots.size(), 0), keep(F.ops.size(), 1);
    auto alldead = [&](int off, int len) { if (off < 0 || off + len > 1024) return false; for (int k = 0; k < len; k++) if (!dead[(size_t)(off + k)]) return false; return true; };
    auto kill = [&](int off, int len) { if (off < 0 || off + len > 1024) return; for (int k = 0; k < len; k++) dead[(size_t)(off + k)] = 1; };
    auto read = [&](int off, int len) { if (off < 0) off = 0; int e = off + len > 1024 ? 1024 : off + len; for (int k = off; k < e; k++) dead[(size_t)k] = 0; };
    auto L = [&](int32_t x) { if (x >= 0 && (size_t)x < live.size()) live[(size_t)x] = 1; };
    for (int i = (int)F.ops.size() - 1; i >= 0; i--) {
        COp& o = F.ops[(size_t)i];
        auto dl = [&]() { return o.dst >= 0 && (size_t)o.dst < live.size() && live[(size_t)o.dst]; };
        switch (o.k) {
            case OP_MARK: break;
            case OP_PUT8: if (alldead(o.off, 8)) { keep[(size_t)i] = 0; break; } L(o.a); kill(o.off, 8); break;
            case OP_PUTU: if (alldead(o.off, o.sz)) { keep[(size_t)i] = 0; break; } kill(o.off, o.sz); break;
            case OP_PUT: { int len = o.b > 32 ? 32 : o.b; if (len > 0 && alldead(o.off, len)) { keep[(size_t)i] = 0; break; } L(o.a); break; }
            case OP_PUTI: break;
            case OP_GET: case OP_GET8: if (!dl()) { keep[(size_t)i] = 0; break; } read(o.off, o.k == OP_GET8 ? 8 : (o.sz ? o.sz : 32)); break;
            case OP_UNOP: if (!dl()) { keep[(size_t)i] = 0; break; } L(o.a); break;
            case OP_BINOP: case OP_ADD64: if (!dl()) { keep[(size_t)i] = 0; break; } L(o.a); L(o.b); break;
            case OP_ITE: if (!dl()) { keep[(size_t)i] = 0; break; } L(o.a); L(o.b); L(o.c); break;
            case OP_CCALL: if (!dl()) { keep[(size_t)i] = 0; break; } for (int a = 0; a < o.b; a++) L(F.args[(size_t)(o.off + a)]); break;
            case OP_LOAD: if (!dl()) o.k = OP_LOADE; L(o.a); break;
            case OP_LOADE: case OP_STOREU: L(o.a); break;
            case OP_STORE: L(o.a); L(o.c); break;
            case OP_STOREG: case OP_LOADG: L(o.a); L(o.b); L(o.c); break;
            case OP_CAS: case OP_LLSC: L(o.a); break;
            case OP_DIRTY: L(o.a); read(0, 1024); break;
        }
    }
    size_t w = 0; for (size_t i = 0; i < F.ops.size(); i++) if (keep[i]) F.ops[w++] = F.ops[i];
    F.ops.resize(w); F.ops.shrink_to_fit();
    F.nemit = 0; for (const COp& o : F.ops) switch (o.k) { case OP_LOAD: case OP_LOADE: case OP_STORE: case OP_STOREU: case OP_STOREG:
        case OP_LOADG: case OP_CAS: case OP_LLSC: case OP_DIRTY: F.nemit++; break; default: break; }
    if (fbuf_.size() < (size_t)F.nemit) fbuf_.resize((size_t)F.nemit);
    if (getenv("PTRECON_FDUMP")) { static const char* KN[] = {"GET","GET8","UNOP","BINOP","ADD64","ITE","CCALL","LOAD","LOADE","PUT","PUT8","PUTU","PUTI","STORE","STOREU","STOREG","LOADG","CAS","LLSC","DIRTY","MARK"};
        fprintf(stderr, "FDUMP %#lx n=%d in=%d out=%zu:", (unsigned long)blks[0]->addr, n, F.ops_in, w - (size_t)n);
        for (auto& o : F.ops) { fprintf(stderr, " %s", KN[o.k]); if (o.k == OP_PUT8 || o.k == OP_PUTU || o.k == OP_GET8 || o.k == OP_PUT || o.k == OP_GET) fprintf(stderr, "@%d", o.off); if (o.k == OP_BINOP || o.k == OP_UNOP) fprintf(stderr, ":%x", o.op); }
        fprintf(stderr, "\n"); }
    n_fused++; n_fused_ops_in += (uint64_t)F.ops_in; n_fused_ops_out += (uint64_t)(w - (size_t)n);
    return true;
}

void VexInterp::exec_fused(FProg& F, const MemAccess** accp, uint32_t* marks) {
    // Direct-threaded dispatch (one indirect jump per handler, GCC labels-as-values) and inline
    // fast paths for fully known 64-bit integer ops; every slow case calls the same unop/binop as
    // run_prog, so results are identical.
    MemAccess* const ab = fbuf_.data(); MemAccess* wp = ab; uint32_t* mp = marks;
    *accp = ab;
    Val* S = F.slots.data();
    const int32_t* A = F.args.data();
    static void* const T[] = { &&L_GET, &&L_GET8, &&L_UNOP, &&L_BINOP, &&L_ADD64, &&L_ITE, &&L_CCALL, &&L_LOAD, &&L_LOADE,
        &&L_PUT, &&L_PUT8, &&L_PUTU, &&L_PUTI, &&L_STORE, &&L_STOREU, &&L_STOREG, &&L_LOADG, &&L_CAS, &&L_LLSC, &&L_DIRTY, &&L_MARK };
    const COp* op = F.ops.data(); const COp* const oe = op + F.ops.size();
#define EMIT(ad, sz_, op_) do { const Val& ea_ = (ad); const bool k_ = ea_.known(); wp->addr = k_ ? ea_.u64() : 0; wp->known = k_; wp->size = (sz_); wp->op = (op_); ++wp; } while (0)
#define NEXT do { if (++op == oe) goto L_END; goto *T[op->k]; } while (0)
    if (op == oe) goto L_END;
    goto *T[op->k];
L_MARK: *mp++ = (uint32_t)(wp - ab); NEXT;
L_GET8: { const COp& o = *op; Val& r = S[o.dst]; memcpy(&r.w[0], g + o.off, 8); r.kn = kn_mask8(gk + o.off); r.sz = 8; NEXT; }
L_GET: { const COp& o = *op; S[o.dst] = get_reg(o.off, o.sz); NEXT; }
L_UNOP: { const COp& o = *op; const Val& a = S[o.a];
    if (a.sz == 8 && a.kn == 0xff && o.op == Iop_64to32) { Val& r = S[o.dst]; const uint64_t w1 = a.w[1], w2 = a.w[2], w3 = a.w[3]; r.w[0] = (uint32_t)a.w[0]; r.w[1] = w1; r.w[2] = w2; r.w[3] = w3; r.kn = a.kn & 0xf; r.sz = 4; NEXT; }
    if (a.sz == 4 && (a.kn & 0xf) == 0xf && o.op == Iop_32Uto64) { Val& r = S[o.dst]; r.w[0] = (uint32_t)a.w[0]; r.w[1] = r.w[2] = r.w[3] = 0; r.kn = 0xff; r.sz = 8; NEXT; }
    S[o.dst] = unop((IROp)o.op, a); NEXT; }
L_ADD64: { const COp& o = *op; const Val& a = S[o.a]; const Val& c = S[o.b];
    if (a.sz == 8 && c.sz == 8) { Val& r = S[o.dst]; r.sz = 8;
        if (a.kn == 0xff && c.kn == 0xff) { r.w[0] = a.w[0] + c.w[0]; r.kn = 0xff; } else { r.w[0] = 0; r.kn = 0; }
        r.w[1] = r.w[2] = r.w[3] = 0; NEXT; }
    S[o.dst] = binop(Iop_Add64, a, c); NEXT; }
L_BINOP: { const COp& o = *op; const Val& a = S[o.a]; const Val& c = S[o.b];
    if (a.sz == 8 && (a.kn & 0xff) == 0xff && ((c.sz == 8 && (c.kn & 0xff) == 0xff) || (c.sz == 1 && (c.kn & 1)))) {
        const uint64_t x = a.w[0], y = c.sz == 8 ? c.w[0] : (c.w[0] & 0xff); uint64_t v; bool ok = true;
        switch (o.op) {
            case Iop_Sub64: v = x - y; break; case Iop_And64: v = x & y; break; case Iop_Or64: v = x | y; break;
            case Iop_Xor64: v = x ^ y; break; case Iop_Mul64: v = x * y; break;
            case Iop_Shl64: if (c.sz != 1) { ok = false; v = 0; break; } v = y >= 64 ? 0 : x << y; break;
            case Iop_Shr64: if (c.sz != 1) { ok = false; v = 0; break; } v = y >= 64 ? 0 : x >> y; break;
            case Iop_Sar64: if (c.sz != 1) { ok = false; v = 0; break; } v = (uint64_t)((int64_t)x >> (y > 63 ? 63 : y)); break;
            default: ok = false; v = 0; break;
        }
        if (ok && (c.sz == 8) == !(o.op == Iop_Shl64 || o.op == Iop_Shr64 || o.op == Iop_Sar64)) {
            Val& r = S[o.dst]; r.w[0] = v; r.w[1] = r.w[2] = r.w[3] = 0; r.kn = 0xff; r.sz = 8; NEXT; }
    }
    S[o.dst] = binop((IROp)o.op, a, c); NEXT; }
L_ITE: { const COp& o = *op; const Val& c = S[o.a];
    if (!c.known()) { const Val& x = S[o.b]; const Val& y = S[o.c];
        Val r = Val::U(x.sz); for (int i = 0; i < x.sz; i++) if (((x.kn >> i) & 1) && ((y.kn >> i) & 1) && x.byte(i) == y.byte(i)) r.set_byte(i, x.byte(i), true);
        S[o.dst] = r; }
    else S[o.dst] = c.u64() ? S[o.b] : S[o.c];
    NEXT; }
L_CCALL: { const COp& o = *op; Val argbuf[8]; for (int i = 0; i < o.b; i++) argbuf[i] = S[A[o.off + i]];
    S[o.dst] = ccall_args(*(const IRNodeC*)o.p, argbuf, (size_t)o.b); NEXT; }
L_LOAD: { const COp& o = *op; const Val& a = S[o.a]; EMIT(a, o.sz, 0); S[o.dst] = a.known() ? shadow_load(a.u64(), o.sz) : Val::U(o.sz); NEXT; }
L_LOADE: { const COp& o = *op; EMIT(S[o.a], o.sz, 0); NEXT; }
L_PUT8: { const COp& o = *op; const Val& v = S[o.a]; if (v.sz != 8) goto L_PUT;
    memcpy(g + o.off, &v.w[0], 8); uint64_t e = kn_expand8(v.kn & 0xffu); memcpy(gk + o.off, &e, 8); NEXT; }
L_PUT: { const COp& o = *op; const Val& v = S[o.a]; int sz = o.b < v.sz ? o.b : v.sz;
    if (o.off >= 0) {
        if (sz > 1024 - o.off) sz = 1024 - o.off;
        if (sz > 32) sz = 32;
        if (sz > 0) { memcpy(g + o.off, v.w, (size_t)sz);
            for (int j = 0; j < sz; j += 8) { uint64_t e = kn_expand8((v.kn >> j) & 0xffu); memcpy(gk + o.off + j, &e, (size_t)(sz - j < 8 ? sz - j : 8)); } } }
    NEXT; }
L_PUTU: { const COp& o = *op; switch (o.sz) {
        case 8: memset(g + o.off, 0, 8); memset(gk + o.off, 0, 8); break;
        case 16: memset(g + o.off, 0, 16); memset(gk + o.off, 0, 16); break;
        case 32: memset(g + o.off, 0, 32); memset(gk + o.off, 0, 32); break;
        default: memset(g + o.off, 0, o.sz); memset(gk + o.off, 0, o.sz); break; }
    NEXT; }
L_PUTI: { const COp& o = *op; set_reg_unknown(o.off, o.b); NEXT; }
L_STORE: { const COp& o = *op; const Val& a = S[o.a]; EMIT(a, o.b, 1); if (a.known()) shadow_store(a.u64(), S[o.c], o.b); NEXT; }
L_STOREU: { const COp& o = *op; const Val& a = S[o.a]; EMIT(a, o.b, 1); if (a.known()) shadow_forget(a.u64(), o.b); NEXT; }
L_STOREG: { const COp& o = *op; const IRStmtC& s = *(const IRStmtC*)o.p; const Val& gd = S[o.c];
    if (!(gd.known() && !gd.u64())) {
        const Val& a = S[o.a];
        if (!gd.known()) { Val ua = Val::U(8); EMIT(ua, s.size, 1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); }
        else { EMIT(a, s.size, 1); if (a.known()) shadow_store(a.u64(), S[o.b], s.size); } }
    NEXT; }
L_LOADG: { const COp& o = *op; const IRStmtC& s = *(const IRStmtC*)o.p; const Val& gd = S[o.c]; int dsz = o.off;
    if (gd.known() && !gd.u64()) { S[o.dst] = S[o.b]; NEXT; }
    const Val& a = S[o.a];
    if (!gd.known()) { Val ua = Val::U(8); EMIT(ua, s.size, 0); S[o.dst] = Val::U(dsz); NEXT; }
    EMIT(a, s.size, 0);
    Val v = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size);
    switch (s.cvt) { case ILGop_16Sto32: case ILGop_8Sto32: v = widen(v, 4, true); break; case ILGop_16Uto32: case ILGop_8Uto32: v = widen(v, 4, false); break; default: break; }
    v.sz = dsz; S[o.dst] = v; NEXT; }
L_CAS: { const COp& o = *op; const IRStmtC& s = *(const IRStmtC*)o.p; Val a = S[o.a]; EMIT(a, s.size, 2);
    int esz = s.tmp2 >= 0 ? s.size / 2 : s.size;
    S[o.dst] = a.known() ? shadow_load(a.u64(), esz) : Val::U(esz);
    if (o.b >= 0) S[o.b] = a.known() ? shadow_load(a.u64() + esz, esz) : Val::U(esz);
    if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size);
    NEXT; }
L_LLSC: { const COp& o = *op; const IRStmtC& s = *(const IRStmtC*)o.p; Val a = S[o.a];
    if (s.e[1] >= 0) { EMIT(a, s.size, 1); S[o.dst] = Val::U(1); if (a.known()) shadow_store(a.u64(), Val::U(s.size), s.size); }
    else { EMIT(a, s.size, 0); S[o.dst] = a.known() ? shadow_load(a.u64(), s.size) : Val::U(s.size); }
    NEXT; }
L_DIRTY: { const COp& o = *op; const IRStmtC& s = *(const IRStmtC*)o.p; if (o.dst >= 0) S[o.dst] = Val::U(o.off);
    if (s.mfx != Ifx_None && s.e[1] >= 0) { Val a = S[o.a]; int mop = s.mfx == Ifx_Read ? 0 : s.mfx == Ifx_Write ? 1 : 2; EMIT(a, s.msize, mop);
        if (a.known() && s.mfx != Ifx_Read) shadow_store(a.u64(), Val::U(s.msize), s.msize); }
    NEXT; }
#undef NEXT
#undef EMIT
L_END:
    *mp = (uint32_t)(wp - ab);
}

bool VexInterp::exec(const IRBlockC& b, std::vector<MemAccess>* sink) {
    if (!b.ok) return false;
    sink_ = sink; use_sink_ = true;
    if (!tree_) {
        if (!b.prog) compile(b);
        const CProg& P = *b.prog;
        if (!P.fallback) {
            if (t_.size() < (size_t)P.nslots) t_.resize(P.nslots);
            run_prog(P);
            use_sink_ = false; return true;
        }
    }
    bool r = exec_tree(b);
    use_sink_ = false;
    return r;
}

