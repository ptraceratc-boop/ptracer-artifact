/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
  Replaces probed_func that has a branch within the bytes which must be overwritten
 */

#include <stdlib.h>

#include "pin.H"
#include <iostream>
#include <fstream>






/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

typedef int (*FUNCPTR)(int);
static int (*pf_jmp_in_probe)(int num);

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool replaces probed_func()\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

/* ===================================================================== */

int ToolReplacementFunc(int num)
{
    std::cout << "ToolReplacementFunc: calling probed_func()" << std::endl << std::flush;
    int retVal = (pf_jmp_in_probe)(num);
    std::cout << "ToolReplacementFunc: back from probed_func()" << std::endl << std::flush;
    return retVal;
}

/* ===================================================================== */

// Called every time a new image is loaded.
// Look for routines that we want to replace.
VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn = RTN_FindByName(img, "probed_func");

    if (RTN_Valid(rtn))
    {
        if (RTN_IsSafeForProbedReplacementEx(rtn, PROBE_MODE_ALLOW_RELOCATION))
        {
            // Save the function pointer that points to the new location of
            // the entry point of the original exit in this image.
            //
            pf_jmp_in_probe = (FUNCPTR)RTN_ReplaceProbedEx(rtn, PROBE_MODE_ALLOW_RELOCATION, AFUNPTR(ToolReplacementFunc));

            std::cout << "ImageLoad: Replaced probed_func() in:" << IMG_Name(img) << std::endl;
        }
        else
        {
            std::cout << "ImageLocad: Can't replace probed_func() in:" << IMG_Name(img) << std::endl;
            exit(-1);
        }
    }
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) return Usage();

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
