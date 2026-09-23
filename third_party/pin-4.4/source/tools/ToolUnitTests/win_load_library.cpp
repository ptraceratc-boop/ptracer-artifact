/*
 * Copyright (C) 2006-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <Windows.h>
#include <stdio.h>

// Combination of thread creation and load library

extern "C" __declspec(dllexport) DWORD WINAPI ThreadRoutine(LPVOID)
{
    // use the loader lock
    for (int i = 0; i < 100000; i++)
    {
        GetModuleHandle("kernel32.dll");
    }
    return 0;
}

int ThreadCreateAndLoadLibrary()
{
    //thread creation
    const long num_threads     = 1;
    HANDLE aThreads[num_threads] = {0};

    for (long cnt_th = 0; cnt_th < num_threads; cnt_th++)
    {
        aThreads[cnt_th] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadRoutine, 0, 0, 0);
        if (NULL == aThreads[cnt_th])
        {
            fprintf(stderr, "CreateThread failed with code %X\n", GetLastError());
            fflush(stderr);
            return 1;
        }
    }
    fprintf(stderr, "created %d threads \n", num_threads);
    fflush(stderr);

    //load library
    const int iterations = 10;
    for (int i = 0; i < iterations; i++)
    {
        HMODULE hKernel32 = LoadLibrary("kernel32.dll");
    }
    fprintf(stderr, "loaded kernel32 %d times \n", iterations);

    //thread termination
    for (long cnt_th = num_threads; 0 < cnt_th; --cnt_th)
    {
        DWORD slot = WaitForMultipleObjects(cnt_th, aThreads, FALSE, INFINITE);
        slot -= WAIT_OBJECT_0;
        if (num_threads <= slot)
        {
            fprintf(stderr, "Wait for threads failed\n");
            fflush(stderr);
            return 2;
        }
        DWORD thread_ret;
        GetExitCodeThread(aThreads[slot], &thread_ret);
        CloseHandle(aThreads[slot]);
        aThreads[slot] = aThreads[cnt_th - 1];
    }
    fprintf(stderr, "all %d threads terminated\n", num_threads);
    fflush(stderr);
    return 0;
}

int main()
{
    return ThreadCreateAndLoadLibrary();
}