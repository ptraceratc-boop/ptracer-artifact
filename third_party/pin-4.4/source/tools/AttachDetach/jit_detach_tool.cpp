/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <assert.h>
#include <tool_macros.h>








/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "jit_tool.out", "specify file name");

std::ofstream TraceFile;
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool tests MT attach in JIT mode.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

PIN_LOCK pinLock;
VOID DetachPinFromMTApplication(unsigned int numOfThreads)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    TraceFile << "Pin tool: sending detach request" << std::endl;
    PIN_ReleaseLock(&pinLock);
    PIN_Detach();
}

int HoldAppThread() { return 1; }

UINT32 threadCounter = 0;
VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    TraceFile << "Thread counter is updated to " << std::dec << (threadCounter + 1) << std::endl;
    ++threadCounter;
    PIN_ReleaseLock(&pinLock);
}
BOOL AllThreadsNotifed(unsigned int numOfThreads)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    // Check that we don't have any extra thread
    assert(threadCounter <= numOfThreads);
    if (threadCounter == numOfThreads)
    {
        TraceFile.close();
        PIN_ReleaseLock(&pinLock);
        return TRUE;
    }
    PIN_ReleaseLock(&pinLock);
    return FALSE;
}

VOID ImageLoad(IMG img, void* v)
{
    RTN rtn = RTN_FindByName(img, C_MANGLE("DetachPin"));
    if (RTN_Valid(rtn))
    {
        TraceFile << "DetachPin instrumented " << std::endl;
        RTN_Replace(rtn, AFUNPTR(DetachPinFromMTApplication));
    }

    rtn = RTN_FindByName(img, C_MANGLE("ThreadHoldByPin"));
    if (RTN_Valid(rtn))
    {
        TraceFile << "ThreadHoldByPin instrumented " << std::endl;
        RTN_Replace(rtn, AFUNPTR(HoldAppThread));
    }

    rtn = RTN_FindByName(img, C_MANGLE("ThreadsReady"));
    if (RTN_Valid(rtn))
    {
        TraceFile << "ThreadsReady instrumented " << std::endl;
        RTN_Replace(rtn, AFUNPTR(AllThreadsNotifed));
    }
}
/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    PIN_InitLock(&pinLock);

    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
