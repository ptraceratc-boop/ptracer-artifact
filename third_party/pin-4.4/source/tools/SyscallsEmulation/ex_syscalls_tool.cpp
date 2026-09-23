/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This tool runs with ex_syscalls_app and verifies that Pin reports the Windows Ex memory syscalls through syscall
 * callbacks. It records syscall numbers on entry, converts them to SYSCALL_KEY values on exit, and counts
 * NtAllocateVirtualMemoryEx, NtMapViewOfSectionEx, and NtUnmapViewOfSectionEx. At process exit it reports success when
 * the expected callbacks were observed, or a skip when the Ex map syscalls consistently return STATUS_NOT_SUPPORTED.
 */

#include "pin.H"

#include <fstream>
#include <iostream>
#include <stack>

#include <windows/pinrt_windows.h>

std::ostream* out              = &std::cerr;
static std::ofstream* outFile  = NULL;
static TLS_KEY tlsKey          = INVALID_TLS_KEY;
static UINT64 allocateExCount           = 0;
static UINT64 mapViewExCount            = 0;
static UINT64 mapViewExSuccessCount     = 0;
static UINT64 mapViewExUnsupportedCount = 0;
static UINT64 unmapViewExCount          = 0;

static const ADDRINT STATUS_NOT_SUPPORTED_VALUE = 0xc00000bb;

// Windows NTSTATUS values are successful when interpreted as non-negative signed 32-bit values.
static BOOL NtSuccess(ADDRINT status) { return (static_cast< INT32 >(status) >= 0); }

// Some Windows/PINOS configurations expose the Ex syscalls but do not implement the map variants.
static BOOL NtNotSupported(ADDRINT status) { return ((status & 0xffffffff) == STATUS_NOT_SUPPORTED_VALUE); }

struct SyscallInfo
{
    // Syscall entry/exit callbacks may nest, so each thread keeps its own stack of entered syscall numbers.
    std::stack< ADDRINT > syscallNumStack;
};

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output file");

INT32 Usage()
{
    std::cerr << "This tool checks Windows Ex memory syscall callbacks." << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

// Lazily allocate per-thread syscall tracking state. ThreadStart covers normal threads; this also handles early callbacks.
static SyscallInfo* GetSyscallInfo(THREADID threadIndex)
{
    SyscallInfo* info = static_cast< SyscallInfo* >(PIN_GetThreadData(tlsKey, threadIndex));
    if (info == NULL)
    {
        info = new SyscallInfo();
        PIN_SetThreadData(tlsKey, info, threadIndex);
    }
    return info;
}

VOID SyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD sysStd, VOID* v)
{
    // Syscall callbacks can be nested, so match each exit with the latest entry on this thread.
    GetSyscallInfo(threadIndex)->syscallNumStack.push(PIN_GetSyscallNumber(ctxt, sysStd));
}

// Match the exiting syscall to its entry, translate the Windows syscall number, and update the Ex syscall counters.
VOID SyscallExit(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD sysStd, VOID* v)
{
    SyscallInfo* info = static_cast< SyscallInfo* >(PIN_GetThreadData(tlsKey, threadIndex));
    if ((info == NULL) || info->syscallNumStack.empty())
    {
        return;
    }

    SYSCALL_KEY key = PIN_GetKeyFromWindowsSyscall(info->syscallNumStack.top());
    info->syscallNumStack.pop();

    ADDRINT status = PIN_GetSyscallReturn(ctxt, sysStd);

    switch (key)
    {
        case SYSCALL_KEY_NtAllocateVirtualMemoryEx:
            ++allocateExCount;
            break;
        case SYSCALL_KEY_NtMapViewOfSectionEx:
            ++mapViewExCount;
            if (NtSuccess(status))
            {
                ++mapViewExSuccessCount;
            }
            else if (NtNotSupported(status))
            {
                ++mapViewExUnsupportedCount;
            }
            break;
        case SYSCALL_KEY_NtUnmapViewOfSectionEx:
            ++unmapViewExCount;
            break;
        default:
            break;
    }
}

// A context change can discard a pending syscall exit, so clear stale entries before tracking resumes on this thread.
VOID OnContextChange(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, const CONTEXT* ctxtFrom, CONTEXT* ctxtTo,
                     INT32 info, VOID* v)
{
    SyscallInfo* syscallInfo = static_cast< SyscallInfo* >(PIN_GetThreadData(tlsKey, threadIndex));
    if (syscallInfo != NULL)
    {
        while (!syscallInfo->syscallNumStack.empty())
        {
            syscallInfo->syscallNumStack.pop();
        }
    }
}

// Allocate per-thread tracking state before the thread starts executing application code.
VOID ThreadStart(THREADID threadIndex, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    PIN_SetThreadData(tlsKey, new SyscallInfo(), threadIndex);
}

// Release the per-thread tracking state created by ThreadStart or GetSyscallInfo.
VOID ThreadFini(THREADID threadIndex, const CONTEXT* ctxt, INT32 code, VOID* v)
{
    SyscallInfo* info = static_cast< SyscallInfo* >(PIN_GetThreadData(tlsKey, threadIndex));
    delete info;
    PIN_SetThreadData(tlsKey, NULL, threadIndex);
}

// Summarize the observed callbacks and classify the run as success, supported skip, or failure.
VOID Fini(INT32 code, VOID* v)
{
    *out << "NtAllocateVirtualMemoryEx count: " << allocateExCount << std::endl;
    *out << "NtMapViewOfSectionEx count: " << mapViewExCount << std::endl;
    *out << "NtMapViewOfSectionEx success count: " << mapViewExSuccessCount << std::endl;
    *out << "NtMapViewOfSectionEx unsupported count: " << mapViewExUnsupportedCount << std::endl;
    *out << "NtUnmapViewOfSectionEx count: " << unmapViewExCount << std::endl;

    if ((allocateExCount > 0) && (mapViewExCount >= 2) && (mapViewExSuccessCount > 0) &&
        (unmapViewExCount >= mapViewExSuccessCount))
    {
        *out << "SUCCESS: observed expected Ex syscalls" << std::endl;
    }
    else if ((allocateExCount > 0) && (mapViewExCount >= 2) && (mapViewExUnsupportedCount == mapViewExCount))
    {
        *out << "SKIP: Ex map syscalls returned STATUS_NOT_SUPPORTED" << std::endl;
    }
    else
    {
        *out << "ERROR: missing expected Ex syscalls" << std::endl;
    }

    if (outFile != NULL)
    {
        delete outFile;
        outFile = NULL;
        out     = &std::cerr;
    }
}

// Initialize Pin, route optional output to a file, register syscall/thread callbacks, and start the application.
int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::string fileName = KnobOutputFile.Value();
    if (!fileName.empty())
    {
        outFile = new std::ofstream(fileName.c_str());
        out     = outFile;
    }

    tlsKey = PIN_CreateThreadDataKey(NULL);
    if (tlsKey == INVALID_TLS_KEY)
    {
        std::cerr << "Failed to create TLS key" << std::endl;
        return -1;
    }

    PIN_AddSyscallEntryFunction(SyscallEntry, NULL);
    PIN_AddSyscallExitFunction(SyscallExit, NULL);
    PIN_AddContextChangeFunction(OnContextChange, NULL);
    PIN_AddThreadStartFunction(ThreadStart, NULL);
    PIN_AddThreadFiniFunction(ThreadFini, NULL);
    PIN_AddFiniFunction(Fini, NULL);

    PIN_StartProgram();
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */

