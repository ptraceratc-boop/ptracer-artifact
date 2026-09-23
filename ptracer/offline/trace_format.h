// PTracer memory trace format (SPEC_FORMAT section 5).  Single source of truth.
#pragma once
#include <stdint.h>
#define MTRACE_MAGIC 0x544D5450u /* "PTMT" little-endian */
#define MTRACE_VERSION 2u
struct mtrace_hdr { uint32_t magic; uint32_t version; uint32_t flags; uint32_t tid_count; uint8_t reserved[16]; };
enum { MTH_TIME_MERGED = 1u << 0, MTH_SAMPLED_OUTPUT = 1u << 1 };
enum { MT_LOAD = 0, MT_STORE = 1, MT_RMW = 2, MT_MARKER = 3 };
enum { MTF_ADDR_UNKNOWN = 1u << 0, MTF_IN_OVERFLOW = 1u << 1 };
struct mtrace_rec { uint64_t addr; uint64_t ip; uint64_t ts; uint32_t tid; uint8_t op; uint8_t size; uint16_t flags; };
#ifdef __cplusplus
static_assert(sizeof(struct mtrace_hdr) == 32, "hdr"); static_assert(sizeof(struct mtrace_rec) == 32, "rec");
#else
_Static_assert(sizeof(struct mtrace_hdr) == 32, "hdr"); _Static_assert(sizeof(struct mtrace_rec) == 32, "rec");
#endif
