/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This tool is meant to be used with the mt_thread.cpp application. It tests the correctness of PIN_AddDetachFunction.
 * We expect to get exactly one detach callback regardless of the number of threads in the application or the number of
 * detach requests.
 * Also check detach callback was called
 */

#include "pin.H"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <cassert>





KNOB< std::string > KnobOutFile(KNOB_MODE_WRITEONCE, "pintool", "o", "jit_instr_detach.out",
                                "Specify file name for the tool's output.");

std::ofstream outfile;

volatile UINT32 instrumentedInstructions = 0; // the number of instructions that were instrumented.
std::atomic_uint32_t runtimeCount {0};        // the number of executed instructions until detaching.
                                              // the instrumented application may be multi-threaded
INT32 threadCounter = 0;
PIN_LOCK pinLock;
volatile bool detached = false;

VOID docount()
{
    ASSERT(!detached, "Analysis function was called after detach ended");
    ++runtimeCount;
}

VOID Instruction(INS ins, VOID* v)
{
    ASSERT(!detached, "Callback function was called after detach ended");
    ++instrumentedInstructions;
    if (threadCounter >= 2)
    {
        OS_THREAD_ID tid = PIN_GetTid();
        outfile << "Thread " << tid << " is requesting detach." << std::endl;
        PIN_Detach();
    }
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);
}

VOID Detach(VOID* v)
{
    if (detached) // sanity check
    {
        // This should never be reached because only one detach request should be executed.
        std::cerr << "TOOL ERROR: jit_instr_detach is executing the Detach callback twice." << std::endl;
        exit(20); // use exit instead of PIN_ExitProcess because we don't know if it is available at this point.
    }
    detached = true;
    outfile << "Pin detached after " << instrumentedInstructions << " instrumented instructions." << std::endl;
    outfile << "Pin detached after " << runtimeCount << " executed instructions." << std::endl;
    outfile.close();
}

VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    ASSERT(!detached, "Callback function was called after detach ended");
    OS_THREAD_ID tid = PIN_GetTid();
    PIN_GetLock(&pinLock, tid);
    outfile << "Thread " << tid << " has started." << std::endl;
    ++threadCounter;
    PIN_ReleaseLock(&pinLock);
}

VOID ThreadEnd(THREADID threadid, const CONTEXT* ctxt, INT32 code, VOID* v)
{
    if (detached) // sanity check
    {
        // This should never be reached because the detach callback is called after all thread fini callbacks.
        std::cerr << "TOOL ERROR: jit_instr_detach - executing thread fini callback after detach." << std::endl;
        exit(30); // use exit instead of PIN_ExitProcess because we don't know if it is available at this point.
    }
    OS_THREAD_ID tid = PIN_GetTid();
    PIN_GetLock(&pinLock, tid);
    outfile << "Thread " << tid << " has ended." << std::endl;
    assert(threadCounter > 0);
    --threadCounter;
    PIN_ReleaseLock(&pinLock);
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    outfile.open(KnobOutFile.Value().c_str());
    if (!outfile.is_open() || outfile.fail())
    {
        std::cerr << "Failed to open output file " << KnobOutFile.Value().c_str() << "." << std::endl;
        PIN_ExitProcess(10);
    }

    PIN_InitLock(&pinLock);

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadEnd, 0);
    PIN_AddDetachFunction(Detach, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
