/*
 * Copyright (C) 2022-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#define THREAD_NUM 8150

static HANDLE hReleaseAllThreads = NULL;
static HANDLE hThreadsSyncSem    = NULL;

DWORD WINAPI ThreadRoutine(LPVOID p_t_ordinal)
{
    DWORD dwReturn = ::WaitForSingleObject(hThreadsSyncSem, 0);

    fprintf(stderr, "Thread id=%d about to block\n", (long)p_t_ordinal);

    if (dwReturn == WAIT_OBJECT_0)
        ::WaitForSingleObject(hReleaseAllThreads, INFINITE);
    else if (dwReturn == WAIT_TIMEOUT)
        ::SetEvent(hReleaseAllThreads);

    fprintf(stderr, "Thread id=%d released\n", (long)p_t_ordinal);

    int i = 0;
    int x = 0;
    for (i = 0; i < 1000; i++)
    {
        x += i;
    }
    return 0;
}

int main()
{
    const long num_threads    = THREAD_NUM;

    fprintf(stderr, "creating %d threads \n", num_threads);

    // Create non-signaled event with manual reset
    hReleaseAllThreads = ::CreateEvent(NULL, TRUE, FALSE, NULL);

    // Create semaphore with initial counter THREAD_NUM - 1
    hThreadsSyncSem = ::CreateSemaphore(NULL, THREAD_NUM - 1, THREAD_NUM, NULL);

    if ((hReleaseAllThreads == NULL) || (hThreadsSyncSem == NULL))
    {
        fprintf(stderr, "ERROR: Sync objects creation failed\n");
        return 1;
    }

    HANDLE aThreads[THREAD_NUM] = {0};
    long cnt_th = 0;
    for (; cnt_th < num_threads; cnt_th++)
    {
        aThreads[cnt_th] =
            CreateThread(NULL, 16 * 1024, (LPTHREAD_START_ROUTINE)ThreadRoutine, (LPVOID)cnt_th, 0, NULL);
        if (NULL == aThreads[cnt_th])
        {
            fprintf(stderr, "CreateThread failed with code %X\n", GetLastError());
            fflush(stderr);
            return 2;
        }
        else
        {
           fprintf(stderr, "Thread %d created\n", cnt_th);
        }
    }

    while (--cnt_th >= 0)
    {
        DWORD res = WaitForSingleObject(aThreads[cnt_th], INFINITE);
        if (WAIT_OBJECT_0 != res)
        {
            fprintf(stderr, "ERROR: wait for thread failed.\n");
            return 3;
        }
        unsigned long thread_ret = 0;
        GetExitCodeThread(aThreads[cnt_th], &thread_ret);
        CloseHandle(aThreads[cnt_th]);
    }
    fprintf(stderr, "all %d threads terminated\n", num_threads);
    fflush(stderr);
    return 0;
}
