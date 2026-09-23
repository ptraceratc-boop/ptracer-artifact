/*
 * Copyright (C) 2022-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>
#include <iostream>
#include "recursive_apc.h"

int main()
{
    // Calling an emulated syscall
    SIZE_T Size          = 500;
    LPVOID VirtualMemory = VirtualAlloc(0, Size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

    // Calling a directly dispatched syscall
    OFSTRUCT _buffer = {0};
    HFILE _hfile_ = OpenFile("", &_buffer, OF_READ);

    // Exercise NtQueryInformationProcess - TBD need Pin ADX

    // Exercise GetThreadContext on current thread to trigger RESULT_SAME_THREAD flow
    CONTEXT ctx;
    RtlZeroMemory(&ctx, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    BOOL ok          = GetThreadContext(GetCurrentThread(), &ctx);

    // Exercise nested syscalls via recursive APCs
    RECURSIVE_APC_CTX apcCtx;
    RunRecursiveApc(10, &apcCtx, NULL);
    return 0;
}