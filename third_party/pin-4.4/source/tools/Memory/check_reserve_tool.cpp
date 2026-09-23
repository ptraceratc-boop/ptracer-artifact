/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */
// This tool prints in FINI if memory was reserved successfully or not.
// It is used to test the reserve memory functionality of Pin.

#include "pin.H"
#include <iostream>
#include <fstream>

std::ostream* out = &std::cerr;

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "",
                                   "Specify file name for the tool's output. If not specified, stderr is used.");

INT32 Usage()
{
    *out << KNOB_BASE::StringKnobSummary() << std::endl;
    return EXIT_FAILURE;
}

VOID Fini(INT32 code, VOID* v)
{
    auto wasReserved = PIN_WasMemoryReservedInLoadTime();
    if (wasReserved)
    {
        *out << "Memory was reserved successfully." << std::endl;
    }
    else
    {
        *out << "Memory was NOT reserved successfully." << std::endl;
    }
}

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    if (!KnobOutputFile.Value().empty())
    {
        out = new std::ofstream(KnobOutputFile.Value().c_str());
    }

    PIN_AddFiniFunction(Fini, NULL);

    PIN_StartProgram();

    return EXIT_SUCCESS;
}