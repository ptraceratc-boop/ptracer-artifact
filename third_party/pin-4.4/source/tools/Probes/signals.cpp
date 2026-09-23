/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Injector blocks and injectee restores signal mask
 * This test verifyes that signal mask was correctly restored after start
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>



INT32 Usage()
{
    std::cerr << "This pin tool tests signal mask.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
