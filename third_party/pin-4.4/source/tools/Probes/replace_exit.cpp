/*
 * Copyright (C) 2007-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include "tool_macros.h"

/* ===================================================================== */
/* Global variables */
/* ===================================================================== */

static void (*pf_exit)(int status);

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool replaces exit using probes." << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* ===================================================================== */
/* Replacement Routines */
/* ===================================================================== */

void before_exit(int code)
{
    //
    std::cout << "inside before_exit (" << code << ")" << std::endl;
}

void my_exit(int code)
{
    std::cout << "inside my_exit (" << code << ")" << std::endl;

    // calls the original function that was saved in pf_exit at the time of replacement
    ASSERTX(NULL != pf_exit);

    pf_exit(code);
}

/* ===================================================================== */
/* Instrumentation Routines */
/* ===================================================================== */

// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    RTN exitRtn = RTN_FindByName(img, C_MANGLE("_exit"));

    if (RTN_Valid(exitRtn))
    {
        if (!RTN_IsSafeForProbedReplacement(exitRtn))
        {
            std::cout << "Cannot replace exit with my_exit in " << IMG_Name(img) << std::endl;
        }
        else
        {
            // Save the function pointer that points to the new location of
            // the entry point of the original exit in this image.

            pf_exit = (void (*)(int))RTN_ReplaceProbed(exitRtn, AFUNPTR(my_exit));

            // This test places an INSERT_BEFORE probe on _exit. _exit is replaced by Pin so we actually
            // placing a probe on probe here.
            ASSERTX(RTN_InsertCallProbed(exitRtn, IPOINT_BEFORE, AFUNPTR(before_exit), IARG_FUNCARG_ENTRYPOINT_VALUE,
                                         0, // pass the first argument to my_exit
                                         IARG_END));

            std::cout << "Inserted probe for _exit() in:" << IMG_Name(img) << " at address " << std::hex << RTN_Address(exitRtn)
                      << std::dec << std::endl;
            std::cout << "Relocated entry point is at address " << std::hex << (ADDRINT)pf_exit << std::dec << std::endl;
        }
    }
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
