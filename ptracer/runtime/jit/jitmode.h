// Objective selection for runtime-assisted tracing. This does not select Pin JIT.
#ifndef PTJIT_MODE_H
#define PTJIT_MODE_H
#include <stddef.h>
#include <string.h>

// Preserve legacy conservative cache names; Fast gets a distinct suffix.
// Fail closed rather than silently truncating a tag into the other mode's cache.
static inline int ptj_objective(char *version, size_t capacity,
                                const char *flag, int *fast)
{
    if (!version || !capacity || !fast) return -1;
    if (flag && strcmp(flag, "0") && strcmp(flag, "1")) return -1;
    size_t length = strnlen(version, capacity);
    if (!length || length == capacity) return -1;
    int selected = flag && !strcmp(flag, "1");
    if (selected && length + 2 > capacity) return -1;
    if (selected) { version[length] = 'f'; version[length + 1] = 0; }
    *fast = selected;
    return 0;
}
#endif
