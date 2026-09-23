/*
 * Copyright (C) 2015-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
//  This tool tests the functionality of PIN_AddSmcDetectedFunction
//
#include <cstdio>
#include "pin.H"

ADDRINT startOfFuncInBuf                     = 0;
ADDRINT expectedSmcDetectedtraceStartAddress = 0;
ADDRINT expectedSmcDetectedtraceEndAddress   = 0;
BOOL foundExpectedSmc                        = FALSE;
int numSmcDetected                           = 0;

/* ================================================================== */

VOID SmcDetected(ADDRINT traceStartAddress, ADDRINT traceEndAddress, VOID* v)
{
    printf("Tool: SmcDetected in range %p - %p\n", (void*)(traceStartAddress), (void*)(traceEndAddress));
    if (expectedSmcDetectedtraceStartAddress == traceStartAddress && expectedSmcDetectedtraceEndAddress == traceEndAddress)
    {
        foundExpectedSmc = TRUE;
    }
    ASSERTX(traceStartAddress != 0);
    ASSERTX(traceEndAddress != 0);
    numSmcDetected++;
}

VOID Trace(TRACE trace, VOID* v)
{
    if (startOfFuncInBuf && TRACE_Address(trace) == startOfFuncInBuf && expectedSmcDetectedtraceStartAddress == 0)
    {
        printf("Tool: TRACE_Address(trace) %p TRACE_EndAddress(trace) %p\n", (void*)TRACE_Address(trace),
               (void*)(TRACE_Address(trace) + TRACE_Size(trace)));
        expectedSmcDetectedtraceStartAddress = TRACE_Address(trace);
        expectedSmcDetectedtraceEndAddress   = TRACE_Address(trace) + TRACE_Size(trace);
    }
}

/* ================================================================== */
/*
 This routine is called once at the end.
*/
VOID Fini(INT32 c, VOID* v) { ASSERTX(foundExpectedSmc); }

static VOID MyNotifyAddress(void* arg)
{
    startOfFuncInBuf = (ADDRINT)arg;
    printf("Tool: buffer address received: %p\n", arg);
    fflush(stdout);
}

static VOID Image(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img)) return;

    RTN Rtn = RTN_FindByName(img, "NotifyAddress");
    if (!RTN_Valid(Rtn))
    {
        fprintf(stderr, "ERROR: Unable to find NotifyAddress function.\n");
        PIN_ExitProcess(1);
    }

    printf("Inserting callback before NotifyAddress in %s\n", IMG_Name(img).c_str());

    RTN_Open(Rtn);
    RTN_InsertCall(Rtn, IPOINT_BEFORE, MAKE_AFUNPTR(MyNotifyAddress), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    RTN_Close(Rtn);
}

/*
 Initialize and begin program execution under the control of Pin
*/
int main(INT32 argc, CHAR** argv)
{
    if (PIN_Init(argc, argv)) return 1;

    PIN_InitSymbols();

    TRACE_AddInstrumentFunction(Trace, 0);

    IMG_AddInstrumentFunction(Image, 0);

    // Register a routine that gets called when the program ends
    PIN_AddFiniFunction(Fini, 0);

    TRACE_AddSmcDetectedFunction(SmcDetected, 0);

    PIN_StartProgram(); // Never returns

    return 0;
}
