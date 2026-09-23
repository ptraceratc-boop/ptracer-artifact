/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>
#include <iostream>

DWORD WINAPI MyThreadProc(LPVOID param)
{
    return 0;
}

int main()
{
    HANDLE hThread = CreateThread(
        NULL,       // default security
        0,          // default stack
        MyThreadProc,
        NULL,       // no parameter
        0,
        NULL
    );

    if (!hThread) 
    {
        std::cerr << "Failed to create thread.\n";
        return 1;
    }
    DWORD tid = GetThreadId(hThread);
    std::cerr<<"[APP] thread ID is "<<tid<<std::endl;

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    return 0;
}