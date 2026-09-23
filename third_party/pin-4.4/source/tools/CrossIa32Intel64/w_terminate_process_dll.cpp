/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define _WIN32_WINNT 0x0400
#include <Windows.h>
#include <iostream>
#include <stdio.h>





static volatile int i = 0;

//Verify that the LFH is available. return true when running under debugger.
bool VerifyLFHAvailable()
{
    if (IsDebuggerPresent())
    {
        //LFH is not available unser debugger
        return true;
    }
    else
    {
        ULONG heapInfo = 2;
        return HeapSetInformation(GetProcessHeap(), HeapCompatibilityInformation, &heapInfo, sizeof(ULONG));
    }
}

BOOL WINAPI DllMain(HANDLE hDll, DWORD dwReason, LPVOID lpReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            if (VerifyLFHAvailable())
            {
                std::cout << "Terminating process in DllMain(PROCESS_ATTACH)" << std::endl << std::flush;
            }
            else
            {
                std::cout << "ERROR: LFH is not available" << std::endl << std::flush;
            }
            TerminateProcess(GetCurrentProcess(), 0);
            i = 12;
            return FALSE;
            break;
        }
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            break;
        default:
            break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) int Something() { return i; }
