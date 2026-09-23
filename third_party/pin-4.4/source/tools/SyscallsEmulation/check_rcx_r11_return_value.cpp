/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <vector>

/**
 * @brief This tool asserts that after returning from syscall (64-bit only):
 * - RCX register contains the address of the instruction after the syscall
 * - R11 register contains the RFLAGS value before the syscall
 *
 * According to Intel SDM, the SYSCALL instruction:
 * - Saves RIP to RCX
 * - Saves RFLAGS to R11
 *
 * The tool uses a per-thread stack to support nested syscalls caused by
 * APCs (Windows) or signals (Linux).  Each SyscallEntry pushes the
 * current {syscallAddr, rflags} pair; each SyscallExit pops and validates.
 */

/* ================================================================== */
// Global variables
/* ================================================================== */

/// One frame per outstanding (not yet returned) syscall.
struct SyscallFrame
{
    ADDRINT syscallAddr;         ///< Address of the syscall instruction
    ADDRINT rflagsBeforeSyscall; ///< RFLAGS captured at entry
};

/// Per-thread data: a stack of in-flight syscall frames.
struct ThreadData
{
    std::vector< SyscallFrame > stack;
};

static TLS_KEY tlsKey;
std::ostream* out = &std::cerr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool asserts that the return values of RCX and R11 registers" << std::endl
              << "after a syscall is exited are correct according to SDM:" << std::endl
              << "  - RCX should contain the address of the instruction right after the syscall" << std::endl
              << "  - R11 should contain the RFLAGS value before the syscall" << std::endl
              << std::endl
              << "Nested syscalls (APCs / signals) are tracked with a per-thread stack." << std::endl
              << std::endl;

    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

VOID SyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ThreadData* tdata = static_cast< ThreadData* >(PIN_GetThreadData(tlsKey, threadIndex));

    SyscallFrame frame;
    frame.syscallAddr         = PIN_GetContextReg(ctxt, LEVEL_BASE::REG_RIP);
    frame.rflagsBeforeSyscall = PIN_GetContextReg(ctxt, LEVEL_BASE::REG_GFLAGS);

    tdata->stack.push_back(frame);
}

VOID SyscallExit(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ThreadData* tdata = static_cast< ThreadData* >(PIN_GetThreadData(tlsKey, threadIndex));

    // Must have a matching entry.
    ASSERT(!tdata->stack.empty(), "SyscallExit without matching SyscallEntry (stack empty)");

    SyscallFrame frame = tdata->stack.back();
    tdata->stack.pop_back();

    ADDRINT rip = PIN_GetContextReg(ctxt, LEVEL_BASE::REG_RIP);
    ADDRINT rcx = PIN_GetContextReg(ctxt, LEVEL_BASE::REG_RCX);
    ADDRINT r11 = PIN_GetContextReg(ctxt, LEVEL_BASE::REG_R11);

    // Validate only when the syscall returns to the very next instruction
    // (syscall instruction is 2 bytes).  Control-flow diversions (e.g. signal
    // delivery, NtContinue) legitimately change RIP.
    if (frame.syscallAddr != (rip - 2))
    {
        *out << "INFO: Control transfer detected at nesting depth " << tdata->stack.size() << ". Syscall at 0x"
             << hexstr(frame.syscallAddr) << " but returning to 0x" << hexstr(rip) << " (expected 0x"
             << hexstr(frame.syscallAddr + 2) << ")" << std::endl;
        return; // Skip validation for redirected syscalls
    }

    // Verify RCX contains the return address (next instruction after syscall)
    ASSERT(rcx == rip, "Depth " + decstr(tdata->stack.size()) + ": RCX mismatch. RCX=0x" + hexstr(rcx) + " RIP=0x" + hexstr(rip));

    // Verify R11 contains the RFLAGS value from before the syscall
    ASSERT(r11 == frame.rflagsBeforeSyscall, "Depth " + decstr(tdata->stack.size()) + ": R11 mismatch. R11=0x" + hexstr(r11) +
                                                 " RFLAGS=0x" + hexstr(frame.rflagsBeforeSyscall));
}

/* ===================================================================== */
// Thread callbacks
/* ===================================================================== */

VOID ThreadStart(THREADID tid, CONTEXT*, INT32, VOID*)
{
    ThreadData* tdata = new ThreadData();
    PIN_SetThreadData(tlsKey, tdata, tid);
}

VOID ThreadFini(THREADID tid, const CONTEXT*, INT32, VOID*)
{
    ThreadData* tdata = static_cast< ThreadData* >(PIN_GetThreadData(tlsKey, tid));
    delete tdata;
}

/*!
 * The main procedure of the tool.
 */
int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::string fileName = KnobOutputFile.Value();

    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }

    // Initialize TLS for per-thread data
    tlsKey = PIN_CreateThreadDataKey(nullptr);
    if (tlsKey == INVALID_TLS_KEY)
    {
        std::cerr << "Failed to create TLS key" << std::endl;
        return 1;
    }

    PIN_AddThreadStartFunction(ThreadStart, nullptr);
    PIN_AddThreadFiniFunction(ThreadFini, nullptr);

    PIN_AddSyscallEntryFunction(SyscallEntry, NULL);
    PIN_AddSyscallExitFunction(SyscallExit, NULL);

    // Allow concurrent syscall callbacks so that nested syscalls
    // (e.g. APCs on Windows, signals on Linux) are properly handled.
    PIN_CONFIGURATION_INFO configInfo = PIN_CreateDefaultConfigurationInfo();
    PIN_SetAllowedConcurrentCallbacks(configInfo, PIN_CALLBACK_TYPE_SYSCALL);

    // Start the program, never returns
    PIN_StartProgram(configInfo);

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
