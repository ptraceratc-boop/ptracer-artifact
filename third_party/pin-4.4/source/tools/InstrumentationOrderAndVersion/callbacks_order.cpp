/*
 * Copyright (C) 2014-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This tool calls PIN_AddThreadFiniFunction, PIN_AddFiniFunction, and PIN_AddThreadStartFunction.
 * Afterward, it calls CALLBACK_SetExecutionOrder() to set the relative order between
 * all the registered callbacks
 */
#include "pin.H"
#include <fstream>





static KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "callbacks_order.out", "specify file name");

static std::ofstream* out;

THREADID myThread = INVALID_THREADID;

static VOID ThreadStart(THREADID threadIndex, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    if (INVALID_THREADID == myThread)
    {
        myThread = threadIndex;
    }
    if (threadIndex == myThread)
    {
        *out << std::dec << "ThreadStart " << ADDRINT(v) << std::endl;
    }
}

static VOID ThreadFini(THREADID tid, CONTEXT const* c, INT32 code, VOID* v)
{
    if (tid == myThread)
    {
        *out << std::dec << "ThreadFini " << ADDRINT(v) << std::endl;
    }
}

static VOID FiniB(INT32 code, VOID* v) { *out << std::dec << "FiniB " << ADDRINT(v) << std::endl; }

static VOID FiniA(INT32 code, VOID* v)
{
    *out << std::dec << "FiniA " << ADDRINT(v) << std::endl;
    out->close();
    delete out;
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    out = new std::ofstream(KnobOutputFile.Value().c_str(), std::ofstream::out);

    PIN_CALLBACK cbThreadFini0 = PIN_AddThreadFiniFunction(ThreadFini, (VOID*)0);
    PIN_CALLBACK cbThreadFini3 = PIN_AddThreadFiniFunction(ThreadFini, (VOID*)3);
    PIN_CALLBACK cbThreadFini4 = PIN_AddThreadFiniFunction(ThreadFini, (VOID*)4);

    PIN_CALLBACK cbAppFiniA = PIN_AddFiniFunction(FiniA, 0);
    PIN_CALLBACK cbAppFiniB = PIN_AddFiniFunction(FiniB, 0);

    PIN_CALLBACK cbThreadStart0 = PIN_AddThreadStartFunction(ThreadStart, (VOID*)0);
    PIN_CALLBACK cbThreadStart1 = PIN_AddThreadStartFunction(ThreadStart, (VOID*)1);
    PIN_CALLBACK cbThreadStart2 = PIN_AddThreadStartFunction(ThreadStart, (VOID*)2);

    CALLBACK_SetExecutionOrder(cbThreadFini0, (CALL_ORDER)(CALL_ORDER_DEFAULT + 0));
    CALLBACK_SetExecutionOrder(cbThreadFini4, (CALL_ORDER)(CALL_ORDER_DEFAULT + 4));
    CALLBACK_SetExecutionOrder(cbThreadFini3, (CALL_ORDER)(CALL_ORDER_DEFAULT + 3));

    CALLBACK_SetExecutionOrder(cbThreadStart2, (CALL_ORDER)(CALL_ORDER_DEFAULT + 2));
    CALLBACK_SetExecutionOrder(cbThreadStart0, (CALL_ORDER)(CALL_ORDER_DEFAULT + 0));
    CALLBACK_SetExecutionOrder(cbThreadStart1, (CALL_ORDER)(CALL_ORDER_DEFAULT + 1));

    CALLBACK_SetExecutionOrder(cbAppFiniA, (CALL_ORDER)(CALL_ORDER_DEFAULT + 10));
    CALLBACK_SetExecutionOrder(cbAppFiniB, (CALL_ORDER)(CALL_ORDER_DEFAULT + 1));

    // Never returns
    PIN_StartProgram();

    return 0;
}
