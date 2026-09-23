// Application-to-Pintool ABI. The marker is deliberately inert without Pin;
// callers must check the acknowledgement and refuse silently uninstrumented runs.
#ifndef PT_PIN_JITBRIDGE_H
#define PT_PIN_JITBRIDGE_H
#include <stdint.h>
#include "../jit/jitsite_format.h"
enum { PT_PIN_ADD = 1, PT_PIN_MOVE = 2, PT_PIN_REMOVE = 3 };
struct PtPinRequest {
    uint64_t version, operation, base, length, sites, count, destination, acknowledged;
};
static_assert(sizeof(PtjSite) == 24, "JIT site ABI changed");
static_assert(sizeof(PtPinRequest) == 64, "Pin bridge ABI changed");
#ifdef PT_PIN_DEFINE_MARKER
extern "C" __attribute__((noinline, visibility("default")))
void ptj_pin_publish(PtPinRequest *request)
{
    __asm__ volatile("" : : "r"(request) : "memory");
}
#else
extern "C" void ptj_pin_publish(PtPinRequest *request);
#endif
#endif
