// A cached PtjSite plan stores offsets, so relocation alone must not change
// the identity of the unplaceable-site set. Keep the legacy form for A/B tests.
#ifndef PTJAVA_CACHE_KEY_H
#define PTJAVA_CACHE_KEY_H
#include "../jitsites.h"
static inline void ptj_hs_avoid_key(const uint64_t *av, int nav, uint64_t base,
                                   int relative, char *out8) {
    if (nav <= 0) { out8[0] = 0; return; }
    PtjSha sh; ptj_sha_init(&sh);
    for (int i = 0; i < nav; ++i) {
        uint64_t value = relative ? av[i] - base : av[i];
        ptj_sha_update(&sh, &value, sizeof value);
    }
    char hex[65]; ptj_sha_hex(&sh, hex);
    memcpy(out8, hex, 8); out8[8] = 0;
}
#endif
