/*
 * Copyright (C) 2008-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define _WIN32_WINNT 0x0400

#include <stdio.h>
#include <windows.h>
#include <iostream>



volatile int doloop = 1;

__declspec(dllexport) int DoLoop() { return doloop; }

DWORD WINAPI ThreadProc1(VOID* p)
{
    while (DoLoop())
    {
        HMODULE h1 = LoadLibraryW(L"version.dll");
        FARPROC a1 = GetProcAddress(h1, "GetFileVersionInfoW");
        Sleep(1);
        FreeLibrary(h1);
    }
    return 0;
}

DWORD WINAPI ThreadProc2(VOID* p)
{
    while (DoLoop())
    {
        static int i = 0;
        i++;
        i          = i % 0x8;
        LPVOID aaa = VirtualAlloc(0, i * 0x1000000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        Sleep(1);
        VirtualFree(aaa, 0, MEM_RELEASE);
    }
    return 0;
}

__declspec(dllexport) void ShortFunction1(DWORD h)
{
    volatile DWORD i = h;
    Sleep(1);
    if (i != 1)
    {
        exit(-1);
    }
}

__declspec(dllexport) void ShortFunction2(DWORD h)
{
    volatile DWORD i = h;
    Sleep(1);
    if (i != 2)
    {
        exit(-1);
    }
}

DWORD WINAPI ThreadProc3(VOID* p)
{
    while (DoLoop())
    {
        ShortFunction1(1);
    }
    return 0;
}

DWORD WINAPI ThreadProc4(VOID* p)
{
    while (DoLoop())
    {
        ShortFunction2(2);
    }
    return 0;
}

DWORD WINAPI ThreadProc5(VOID* p)
{
    const uintptr_t recursionDepth = uintptr_t(p);
    if (recursionDepth >= 3)
    {
        // Immediate exit from thread start routine and, as a result, thread termination,
        // may increase chances of native thread ID reuse by OS.
        // It is better to retain this application behavior to catch Pin problems related
        // to this native thread ID reuse by OS.
        //Sleep(100);
        return 0;
    }

    const uintptr_t childRecursionDepth = recursionDepth + 1;
    HANDLE threads[2];
    while (DoLoop())
    {
        threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc5, (VOID*)childRecursionDepth, 0, NULL);
        if (NULL == threads[0]) exit(-4);
        threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc5, (VOID*)childRecursionDepth, 0, NULL);
        if (NULL == threads[1]) exit(-4);
        WaitForMultipleObjects(2, threads, TRUE, INFINITE);
        CloseHandle(threads[0]);
        CloseHandle(threads[1]);
    }
    return 0;
}

//Wait for a process completion
//Verify it returned the expected exit code
bool WaitAndVerify(HANDLE process)
{
    if (WaitForSingleObject(process, INFINITE) == WAIT_FAILED)
    {
        std::cerr << "WaitForSingleObject failed" << std::endl << std::flush;
        return FALSE;
    }
    DWORD processExitCode;
    if (GetExitCodeProcess(process, &processExitCode) == FALSE)
    {
        std::cerr << "GetExitCodeProcess Failed" << std::endl << std::flush;
        return FALSE;
    }
    if (processExitCode != 0)
    {
        std::cerr << "Got unexpected exit code" << std::endl << std::flush;
        return FALSE;
    }
    return TRUE;
}

typedef WINADVAPI BOOL WINAPI OPEN_PROCESS_TOKEN_FUNC(HANDLE, DWORD, PHANDLE);

typedef WINADVAPI BOOL WINAPI CREATE_PROCESS_AS_USER_A_FUNC(HANDLE, LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                                                            BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);

DWORD WINAPI ThreadProc6(VOID* p)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    while (DoLoop())
    {
        {
            memset(&si, 0, sizeof(si));
            si.cb = sizeof(STARTUPINFO);
            memset(&pi, 0, sizeof(pi));
            std::string cmdLine = "cmd /c";

            HMODULE advapi32Handle = LoadLibraryW(L"advapi32.dll");
            if (advapi32Handle == NULL)
            {
                std::cerr << "w_app2 failed! (load library advapi32.dll)" << std::endl << std::flush;
                exit(-1);
            }
            HANDLE tokenHandle;
            if (!((OPEN_PROCESS_TOKEN_FUNC*)GetProcAddress(advapi32Handle, "OpenProcessToken"))(GetCurrentProcess(),
                                                                                                TOKEN_ALL_ACCESS, &tokenHandle))
            {
                std::cerr << "w_app2 failed! (open process token)" << std::endl << std::flush;
                exit(-1);
            }
            if (!((CREATE_PROCESS_AS_USER_A_FUNC*)GetProcAddress(advapi32Handle, "CreateProcessAsUserA"))(
                    tokenHandle, NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                std::cerr << "w_app2 failed! (create process as user)" << std::endl << std::flush;
                exit(-1);
            }
            else
            {
                if (!WaitAndVerify(pi.hProcess))
                {
                    std::cerr << "w_app2 failed! (on WaitAndVerify())" << std::endl << std::flush;
                    exit(-1);
                }
                CloseHandle(tokenHandle);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }

            FreeLibrary(advapi32Handle);
        }

        {
            memset(&si, 0, sizeof(si));
            si.cb = sizeof(STARTUPINFO);
            memset(&pi, 0, sizeof(pi));
            std::string cmdLine = "cmd /c";

            if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                std::cerr << "w_app2 failed! (create process)" << std::endl << std::flush;
                exit(-1);
            }
            else
            {
                if (!WaitAndVerify(pi.hProcess))
                {
                    std::cerr << "w_app2 failed! (on WaitAndVerify())" << std::endl << std::flush;
                    exit(-1);
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        Sleep(1);
    }
    return 0;
}

volatile LONG globalQueueCnt = 0;

VOID CALLBACK My_APCProc(ULONG_PTR dwParam)
{
    if (dwParam == 1)
    {
        InterlockedDecrement(&globalQueueCnt);
        return;
    }
    QueueUserAPC(My_APCProc, GetCurrentThread(), dwParam - 1);
    DWORD status = SleepEx(10, true);
}

DWORD WINAPI ThreadProc7(VOID* p)
{
    while (DoLoop())
    {
        DWORD status = SleepEx(1000, true);
    }
    return 0;
}

DWORD WINAPI ThreadProc8(VOID* p)
{
    while (DoLoop())
    {
        InterlockedIncrement(&globalQueueCnt);
        QueueUserAPC(My_APCProc, HANDLE(p), 2);
        while (DoLoop() && (InterlockedCompareExchange(&globalQueueCnt, 0, 0) != 0))
        {
            Sleep(50);
        }
    }
    return 0;
}

void DivideByZero()
{
    static int zero = 0;
    int i           = 1 / zero;
}

DWORD WINAPI ThreadProc9(VOID* p)
{
    while (DoLoop())
    {
        __try
        {
            DivideByZero();
        }
        __except (GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            volatile int j = 11;
        }
        Sleep(10);
    }
    return 0;
}

DWORD WINAPI ThreadProc10(VOID* p)
{
    while (DoLoop())
    {
        Sleep(20);
    }
    return 0;
}

DWORD WINAPI ThreadProc11(VOID* p)
{
    while (DoLoop())
    {
        if (SuspendThread(HANDLE(p)) != (DWORD)-1)
        {
            CONTEXT context;
            context.ContextFlags = CONTEXT_FULL;
            Sleep(1);
            if (GetThreadContext(HANDLE(p), &context))
            {
                Sleep(1);
                SetThreadContext(HANDLE(p), &context);
            }
            Sleep(1);
            ResumeThread(HANDLE(p));
            Sleep(1);
        }
    }
    return 0;
}

int main()
{
    constexpr int NUM_THREADS = 64;
    HANDLE threads[NUM_THREADS];
    threads[0] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc1, NULL, 0, NULL);
    threads[1] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc2, NULL, 0, NULL);
    threads[2] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc2, NULL, 0, NULL);
    threads[3] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc2, NULL, 0, NULL);
    threads[4] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc2, NULL, 0, NULL);
    for (int i = 5; i < 30; i++)
    {
        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc3, NULL, 0, NULL);
    }
    for (int i = 30; i < 57; i++)
    {
        threads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc4, NULL, 0, NULL);
    }
    uintptr_t recursionDepth = 0;
    threads[57]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc5, (VOID*)recursionDepth, 0, NULL);
    threads[58]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc6, NULL, 0, NULL);
    threads[59]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc7, NULL, 0, NULL);
    threads[60]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc8, (VOID*)(threads[59]), 0, NULL);
    threads[61]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc9, NULL, 0, NULL);
    threads[62]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc10, NULL, 0, NULL);
    threads[63]          = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc11, (VOID*)(threads[62]), 0, NULL);

    DWORD ret = 0;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        if (NULL == threads[i])
        {
            std::cerr << "Failure to create thread " << i << std::endl << std::flush;
            ++ret;
        }
    }
    if (0 != ret)
    {
        exit(-2);
    }
    HANDLE susThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc1, NULL, CREATE_SUSPENDED, NULL);
    if (NULL == susThread)
    {
        std::cerr << "Failure to create suspended thread!" << std::endl << std::flush;
        exit(-3);
    }
    ret = WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 180 * 1000);
    TerminateThread(susThread, 0);
    WaitForSingleObject(susThread, 20 * 1000);
    if ((ret == WAIT_TIMEOUT) || (ret == WAIT_FAILED))
    {
        std::cerr << "w_app2 failed!" << std::endl << std::flush;
        doloop = 0;
        // Let the threads the opportunity to terminate cleanly
        WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 20 * 1000);
        exit(-1);
    }
    return 0;
}
