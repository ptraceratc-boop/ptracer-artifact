// Diagnostic fail-closed progress limit. It does NOT skip instructions, repair
// code, resynchronize, or claim that a repeated decode was actually executed.
#pragma once
#include <cstdint>
struct DecodeProgress {
    uint64_t limit=0, previous=~0ull, repeated=0;
    explicit DecodeProgress(uint64_t maximum=0):limit(maximum) {}
    bool stalled(uint64_t offset) {
        repeated = offset==previous ? repeated+1 : 0;
        previous=offset;
        return limit && repeated>=limit;
    }
};
