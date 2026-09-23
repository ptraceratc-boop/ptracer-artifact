/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Test detach from an analysis routine (in Jit). Also check detach callback was called
 */
#include <stdio.h>
#include "pin.H"
#include <iostream>
#include <fstream>





UINT64 icount = 0;

UINT32 icountMax       = 10000;
volatile bool detached = false; // True if detach callback was called

std::ofstream outfile;

KNOB< std::string > KnobOutFile(KNOB_MODE_WRITEONCE, "pintool", "o", "jit_anls_detach.out",
                                "Specify file name for the tool's output.");

VOID docount()
{
    ASSERT(!detached, "Analysis function was called after detach ended");
    if (++icount == icountMax)
    {
        outfile << "Send detach request form analysis routine after " << icount << " instructions." << std::endl;
        PIN_Detach();
    }
}

VOID Instruction(INS ins, VOID* v)
{
    ASSERT(!detached, "Instrumentation function was called after detach ended");
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
    outfile << "Pin detached after " << icount << " executed instructions." << std::endl;
    outfile.close();
}

VOID Fini(INT32 code, VOID* v)
{
    std::cerr << "Count: " << icount << std::endl;
    ASSERT(0, "Error, Fini called although we detached");
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    outfile.open(KnobOutFile.Value().c_str());
    if (!outfile.good())
    {
        std::cerr << "Failed to open output file " << KnobOutFile.Value().c_str() << "." << std::endl;
        PIN_ExitProcess(10);
    }

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_AddDetachFunction(Detach, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
