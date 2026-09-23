/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include "footprint.H"




#if defined(EMX_INIT)
#include "emx-init.H"
#endif

int usage()
{
    std::cerr << "This pin tool computes memory footprints for loads, stores and code references" << std::endl << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

footprint_t footprint;

int main(int argc, char** argv)
{
    if (PIN_Init(argc, argv)) return usage();
    PIN_InitSymbols();
    footprint.activate();
#if defined(EMX_INIT)
    emx_init();
#endif
    PIN_StartProgram(); // Never returns
    return 0;
}
