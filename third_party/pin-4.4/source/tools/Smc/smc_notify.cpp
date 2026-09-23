/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

#if defined(TARGET_WINDOWS)
__declspec(dllexport) __declspec(noinline)
#else
__attribute__((noinline))
#endif
    void NotifyAddress(void* buf)
{
    printf("In NotifyAddress\n");
}
