// Optional raw-JIT stack anchors; applied AFTER copying the unmodified cached plan.
#ifndef PTJIT_ANCHOR_H
#define PTJIT_ANCHOR_H
#include "jitsite_format.h"
#include <string.h>

// mode 0: unchanged, 1: rsp, 2: rsp+rbp. Never remove a selected value, mutate a
// cache entry or claim that the resulting site was actually patched. Return the
// number of added register values, or -1 without modification on invalid input.
static inline int ptj_stack_anchor(PtjSite *sites, int *count, int capacity, int mode) {
    if (!sites || !count || *count < 0 || *count > capacity || mode < 0 || mode > 2) return -1;
    if (!mode) return 0;
    unsigned missing = mode == 1 ? (1u << 4) : (1u << 4) | (1u << 5);
    unsigned next_id = 0;
    for (int i = 0; i < *count; ++i) {
        const auto &s = sites[i];
        if (s.nregs > PTJ_MAXREG) return -1;
        if (s.id >= next_id) next_id = (unsigned)s.id + 1;
        if (s.off || s.kind != PTJ_KIND_REG || s.when != PTJ_WHEN_BEFORE || s.kf) continue;
        for (int k = 0; k < s.nregs; ++k) if (s.regs[k] < 16) missing &= ~(1u << s.regs[k]);
    }
    if (!missing) return 0;
    if (*count == capacity || next_id >= 0xffff) return -1; // 0xffff is reserved for GT
    PtjSite s{};
    s.id = (uint16_t)next_id;
    s.kind = PTJ_KIND_REG; s.when = PTJ_WHEN_BEFORE; s.size = 8;
    // flags_dead stays false: adding an anchor must not manufacture liveness.
    for (int r = 4; r <= 5; ++r) if (missing & (1u << r)) s.regs[s.nregs++] = (uint8_t)r;
    sites[(*count)++] = s;
    return s.nregs;
}
#endif
