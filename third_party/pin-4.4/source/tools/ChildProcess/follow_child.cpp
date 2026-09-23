/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>





KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "inscount.out", "specify output file name");

std::ofstream OutFile;

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    std::cout << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* ===================================================================== */
VOID Fini(INT32 code, VOID* v)
{
    OutFile << "In follow_child PinTool" << std::endl;
    OutFile.close();
}

/* ===================================================================== */

int main(INT32 argc, CHAR** argv)
{
    if (PIN_Init(argc, argv)) return Usage();

    // If the file is not appended to, every instance of the pintool will overwrite it with its own output.
    OutFile.open(KnobOutputFile.Value().c_str(), std::ofstream::app);

    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
