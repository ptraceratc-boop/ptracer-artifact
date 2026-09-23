/*
 * Copyright (C) 2011-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <fstream>
#include "pin.H"

const UINT32 val2  = 0xbaadf00d;
const ADDRINT val3 = 0xacdcacdcacdcacdc;

extern "C" ADDRINT uint32Glob  = 0;
extern "C" ADDRINT addrIntGlob = 0;
int first                      = TRUE;





extern "C" VOID PIN_FAST_ANALYSIS_CALL GetConsts(UINT32 uint32, ADDRINT addrint, ADDRINT whereToSave);
/*
{ 
    uint32Glob = uint32; 
    addrIntGlob = addrint;
}
*/

VOID Trace(TRACE trace, VOID* v)
{
    if (first)
    {
        BBL_InsertCall(TRACE_BblHead(trace), IPOINT_BEFORE, MAKE_AFUNPTR(GetConsts), IARG_FAST_ANALYSIS_CALL, IARG_UINT32, val2,
                       IARG_ADDRINT, val3, IARG_END);
        first = FALSE;
    }
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID* v)
{
    std::cout << std::hex << " uint32Glob " << uint32Glob << " addrIntGlob " << addrIntGlob << std::endl;
    if (uint32Glob != val2)
    {
        std::cout << "***ERROR uint32Glob" << std::endl;
        exit(-1);
    }
    if (addrIntGlob != val3)
    {
        std::cout << "***ERROR addrIntGlob" << std::endl;
        exit(-1);
    }
}

// argc, argv are the entire command line, including pin -t <toolname> -- ...
int main(int argc, char* argv[])
{
    // Initialize pin
    PIN_Init(argc, argv);

    // Register Instruction to be called to instrument instructions
    TRACE_AddInstrumentFunction(Trace, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
