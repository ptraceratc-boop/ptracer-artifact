/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* 
This tool just gets a string knob as an argument so we can call it 
with some value that starts with "--" 
and verify that Pin's argument parser handles it correctly. 
*/

#include "pin.H"

#include <iostream>
#include <fstream>

KNOB< std::string > KnobMyStr(KNOB_MODE_WRITEONCE, "pintool", "myStr", "", "double dash test knob");

VOID Fini(INT32, VOID*)
{
    // Print the value at program end
    std::cout << "Tool arg: " << KnobMyStr.Value() << std::endl;
}

int main(int argc, char* argv[])
{
    // Initialize PIN and KNOB system
    if (PIN_Init(argc, argv))
    {
        return -1;
    }

    // Register Fini callback
    PIN_AddFiniFunction(Fini, nullptr);

    // Launch program under PIN
    PIN_StartProgram();
    return 0;
}
