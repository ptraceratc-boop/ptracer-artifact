/*
 * Copyright (C) 2022-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include "pin.H"

// This tool causes Pin stack overflow exception and validates its proper handling.

// This knob defines Pin stack size in the same units as -thread_stack_size Pin knob.
// It is assumed that values of these knobs match for the test to work properly.
// Test launcher should ensure this.
KNOB< UINT32 > KnobStackSize(KNOB_MODE_WRITEONCE, "pintool", "stack_size", "1021",
                             "Size of the thread stack in KBytes when in PIN state");

///FIXME: Currently there is no test using this flag because Pin doesn't properly handle stack overflow in internal threads.
KNOB< BOOL > KnobOverflowInternalThread(KNOB_MODE_WRITEONCE, "pintool", "overflow_internal_thread", "0",
                                        "If set to 1, then overflow the stack of an internal Pin thread.");

PIN_THREAD_UID stackOverflowThreadId;

// Exception handler
static void OnException(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, const CONTEXT* ctxtFrom, CONTEXT* ctxtTo, INT32 info,
                        VOID* v)
{
    ///FIXME: Modify test to verify this handler is called on stack overflow.
    if (reason == CONTEXT_CHANGE_REASON_EXCEPTION || reason == CONTEXT_CHANGE_REASON_SIGNAL)
    {
        std::cerr << "Tool: Start handling exception." << std::endl;
        std::cerr.flush();
        UINT32 exceptionCode = info;
        ADDRINT exceptAddr   = PIN_GetContextReg(ctxtFrom, REG_INST_PTR);
        std::cerr << "Tool: Exception code " << std::hex << exceptionCode << "."
                  << " Context IP " << std::hex << exceptAddr << "." << std::endl;
        std::cerr.flush();
    }
}

// Compile optimizations are disabled for StackConsume() as gcc is generating assembly
// with rep stos instructions that cause access violation instead of stack overflow.
// optimization is restored immediately after the end of the method
#ifdef TARGET_LINUX
#pragma GCC push_options
#pragma GCC optimize("O0")
#endif

ADDRINT stackConsume(UINT32 depth)
{
    ADDRINT arr[1024];
    // It is necessary to fill the array using external non-inlineable function invocation
    // in order to avoid compiler optimizations that may eliminate array allocation in function stack frame.
    memset(arr, (int)depth, sizeof(arr));
    if (depth > 0)
    {
        // Use access to array to further supress compiler optimizations.
        return stackConsume(depth - 1) + arr[depth];
    }
    else
    {
        return arr[0];
    }
}

#ifdef TARGET_LINUX
#pragma GCC pop_options
#endif

VOID doStackOverflow(VOID*)
{
    static BOOL runOnce = TRUE;
    if (runOnce)
    {
        runOnce = FALSE;
        // Call stack overflow function once before first executed instruction.
        // The test fills stack in units of 1K ADDRINT entries.
        // Recursion depth is specified in the stack fill units.
        // The number of stack fill units, corresponding to the specified stack size,
        // should confidently exceed actual stack size to guarantee stack overflow condition.
        std::cerr << stackConsume((KnobStackSize.Value() / sizeof(ADDRINT)) + 16);
    }
}

VOID insCallback(INS ins, void* v) { INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(doStackOverflow), IARG_INST_PTR, IARG_END); }

static VOID PrepareForFini(VOID*)
{
    // Wait for the internal thread to finish.
    INT32 exitCode = 0;
    PIN_WaitForThreadTermination(stackOverflowThreadId, PIN_INFINITE_TIMEOUT, &exitCode);
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);
    std::cerr << std::hex;
    std::cerr.setf(std::ios::showbase);

    if (KnobOverflowInternalThread)
    {
        // Overflow stack of an internal Pin thread.
        PIN_SpawnInternalThread(doStackOverflow, 0, KnobStackSize.Value() * 1024, &stackOverflowThreadId);
        LEVEL_PINCLIENT::PIN_AddPrepareForFiniFunction(PrepareForFini, 0);
    }
    else
    {
        // Overflow stack of the main application thread.
        INS_AddInstrumentFunction(insCallback, 0);
    }
    PIN_AddContextChangeFunction(OnException, 0);
    // Never returns
    PIN_StartProgram();

    return 0;
}
