/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>

extern void ExecuteRepInstruction(unsigned char* dest, const unsigned char* src, size_t len);

int main(void)
{
    const unsigned char src[] = "rep instruction test application";
    unsigned char       dest[sizeof(src)] = { 0 };

    ExecuteRepInstruction(dest, src, sizeof(src));

    for (size_t i = 0; i < sizeof(src); ++i)
    {
        if (dest[i] != src[i]) return 1;
    }

    return 0;
}