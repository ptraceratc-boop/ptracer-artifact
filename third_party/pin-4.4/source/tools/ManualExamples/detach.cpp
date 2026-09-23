/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "pin.H"
#include <iostream>



// This tool shows how to detach Pin from an
// application that is under Pin's control.

UINT64 icount = 0;

#define N 10000
VOID docount()
{
    icount++;

    // Release control of application if 10000
    // instructions have been executed
    if ((icount % N) == 0)
    {
        PIN_Detach();
    }
}

VOID Instruction(INS ins, VOID* v) { INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END); }

VOID ByeWorld(VOID* v) { std::cerr << std::endl << "Detached at icount = " << N << std::endl; }

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool demonstrates how to detach Pin from an " << std::endl;
    std::cerr << "application that is under Pin's control" << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    // Callback function to invoke for every
    // execution of an instruction
    INS_AddInstrumentFunction(Instruction, 0);

    // Callback functions to invoke before
    // Pin releases control of the application
    PIN_AddDetachFunction(ByeWorld, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
