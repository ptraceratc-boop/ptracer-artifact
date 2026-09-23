// HotSpot arraycopy stubs can branch to one another after the frame prologue.
// Protect only a verified byte boundary; do not infer it from a name alone.
#ifndef PTJAVA_STUB_ENTRY_H
#define PTJAVA_STUB_ENTRY_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t ptj_hs_arraycopy_entry(const uint8_t *code, size_t len, const char *name) {
    // HotSpot17 uses 48 8b ec. The equivalent 48 89 e5 is common in
    // compiler-generated code; recognize both, but not just any MOV opcode.
    static const uint8_t hotspot[] = {0x55, 0x48, 0x8b, 0xec};
    static const uint8_t equivalent[] = {0x55, 0x48, 0x89, 0xe5};
    return code && name && len > sizeof hotspot && strstr(name, "arraycopy") &&
           (!memcmp(code, hotspot, sizeof hotspot) ||
            !memcmp(code, equivalent, sizeof equivalent)) ? sizeof hotspot : 0;
}
#endif
