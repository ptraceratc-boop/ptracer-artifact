/*
 * Copyright (C) 2022-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <errno.h>
#include <sys/syscall.h>
#include <windows/pinrt_windows.h>
#include <stack>

/* ================================================================== */
// Global variables
/* ================================================================== */

std::ostream* out = &std::cerr;

// Thread-local storage key for storing syscall information
static TLS_KEY tlsKey;

// Structure to store syscall information in TLS
struct SyscallInfo 
{
    // Stack because of syscall interruption 
    std::stack<ADDRINT> syscallNumStack {};    
};

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

// Syscall entry callback - save syscall information to TLS
VOID SyscallEntry(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) 
{
    SyscallInfo* info = static_cast<SyscallInfo*>(PIN_GetThreadData(tlsKey, threadIndex));
    if (nullptr == info) 
    {
        info = new SyscallInfo();
        PIN_SetThreadData(tlsKey, info, threadIndex);
    }
    info->syscallNumStack.push(PIN_GetSyscallNumber(ctxt, std));
}

// Syscall exit callback - retrieve syscall information from TLS
VOID SyscallExit(THREADID threadIndex, CONTEXT *ctxt, SYSCALL_STANDARD std, VOID *v) 
{
    SyscallInfo* info = static_cast<SyscallInfo*>(PIN_GetThreadData(tlsKey, threadIndex));
    if(nullptr == info || info->syscallNumStack.empty())
    {
        return;
    }
    SYSCALL_KEY key =  PIN_GetKeyFromWindowsSyscall(info->syscallNumStack.top());
    info->syscallNumStack.pop();

    if((SYSCALL_KEY_NtCreateThread == key) || (SYSCALL_KEY_NtCreateThreadEx == key))
    {
        ADDRINT threadHandlePtr = PIN_GetSyscallArgument(ctxt, std, 0);
        WINDOWS::HANDLE threadHandle;
        PIN_SafeCopy(&threadHandle, (void*)threadHandlePtr, sizeof(threadHandle));
        auto tid = WINDOWS::GetThreadId(threadHandle);
        *out << "tid is " << tid << std::endl;
    }
    
}

// Context change callback to handle interrupted system calls
VOID OnContextChange(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, 
                    const CONTEXT *ctxtFrom, CONTEXT *ctxtTo, INT32 info, VOID *v)
{
    SyscallInfo* syscallInfo = static_cast<SyscallInfo*>(PIN_GetThreadData(tlsKey, threadIndex));
    if (syscallInfo) 
    {
        // Clear the stack to avoid mismatched entry/exit pairs
        while (!syscallInfo->syscallNumStack.empty()) 
        {
            syscallInfo->syscallNumStack.pop();
        }
    }
}

// Thread start callback to initialize TLS
VOID ThreadStart(THREADID threadIndex, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    SyscallInfo* info = new SyscallInfo();
    PIN_SetThreadData(tlsKey, info, threadIndex);
}

// Thread finish callback to cleanup TLS
VOID ThreadFini(THREADID threadIndex, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    SyscallInfo* info = static_cast<SyscallInfo*>(PIN_GetThreadData(tlsKey, threadIndex));
    if (info) 
    {
        // Clear the stack before deletion
        while (!info->syscallNumStack.empty()) 
        {
            info->syscallNumStack.pop();
        }
        delete info;
    }
    PIN_SetThreadData(tlsKey, nullptr, threadIndex);
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::string fileName = KnobOutputFile.Value();

    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }

    tlsKey = PIN_CreateThreadDataKey(nullptr);
    if (INVALID_TLS_KEY == tlsKey) 
    {
        std::cerr << "Failed to create TLS key" << std::endl;
        return -1;
    }

     // Register callbacks
    PIN_AddSyscallEntryFunction(SyscallEntry, 0);
    PIN_AddSyscallExitFunction(SyscallExit, 0);
    PIN_AddContextChangeFunction(OnContextChange, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */