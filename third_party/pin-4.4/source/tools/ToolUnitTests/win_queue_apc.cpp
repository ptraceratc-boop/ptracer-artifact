/*
 * Copyright (C) 2006-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "recursive_apc.h"
#include <stdio.h>

static void PrintSleepExStatus(DWORD status, ULONG_PTR depth)
{
    printf("SleepEx status = 0x%x \n", status);
    fflush(stdout);
}

int main()
{
    RECURSIVE_APC_CTX ctx;
    ULONG_PTR recursiveDepth = 10;

    RunRecursiveApc(recursiveDepth, &ctx, PrintSleepExStatus);

    // The top-level SleepEx (inside RunRecursiveApc) also returns 0xc0.
    printf("SleepEx status = 0x%x \n", (unsigned)WAIT_IO_COMPLETION);
    printf("Number of visits in My_APCProc = %d \n", ctx.visits);
    fflush(stdout);

    return 0;
}
