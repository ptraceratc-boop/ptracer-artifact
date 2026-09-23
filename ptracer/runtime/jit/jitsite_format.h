// Shared site layout: analyzer clients, native patchers and the Pin JIT bridge.
#ifndef PTJIT_SITE_FORMAT_H
#define PTJIT_SITE_FORMAT_H
#include <stdint.h>
enum { PTJ_WHEN_BEFORE = 0, PTJ_WHEN_AFTER = 1 };
enum { PTJ_KIND_REG = 0, PTJ_KIND_LOAD = 1, PTJ_KIND_MEMOP = 2, PTJ_KIND_GT = 3 };
#define PTJ_MAXREG 6
struct PtjSite {
  uint32_t off;       // offset in the original code object
  uint16_t id;        // original analyzer order
  uint8_t when, kind, size, nregs, flags_dead, resync;
  uint32_t kf;        // 0 = every execution; otherwise keyframe period
  uint8_t regs[PTJ_MAXREG]; // 0..15 GP, 16..31 XMM; 255 unsupported
};
#endif
