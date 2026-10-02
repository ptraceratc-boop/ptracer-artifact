// vexval.h -- the known/unknown lattice value of the VEX interpreter (split out of vexinterp.h so
// the compiled block form in vexlift.h can hold constants).
#pragma once
#include <cstdint>

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

