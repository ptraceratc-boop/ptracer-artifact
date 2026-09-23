/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "pin.H"


KNOB< std::string > KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "o", "apc_monitor.out", "output file");
KNOB< INT32 > KnobMinApcCount(KNOB_MODE_WRITEONCE, "pintool", "n", "0", "Minimum number of expected APCs");

FILE* out;
int numApc = 0;

VOID Fini(INT32 code, VOID* v)
{
    fprintf(out, "Number of APCs = %d \n", numApc);
    fclose(out);
    ASSERTX(numApc >= KnobMinApcCount.Value());
}

static void OnApc(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, const CONTEXT* ctxtFrom, CONTEXT* ctxtTo, INT32 info,
                  VOID* v)
{
    if (reason == CONTEXT_CHANGE_REASON_APC)
    {
        ++numApc;
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    out = fopen(KnobOutput.Value().c_str(), "w");

    PIN_AddContextChangeFunction(OnApc, 0);

    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
