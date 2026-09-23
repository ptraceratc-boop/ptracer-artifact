/*
 * Copyright (C) 2006-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//  Replace an original function with a custom function defined in the tool using
//  probes.  The replacement function has a different signature from that of the
//  original replaced function.

#include "pin.H"
#include <iostream>







typedef VOID* (*FP_MALLOC)(size_t);

// This is the replacement routine.
//
VOID* NewMalloc(FP_MALLOC orgFuncptr, UINT32 arg0, ADDRINT returnIp)
{
    // Normally one would do something more interesting with this data.
    //
    std::cout << "NewMalloc (" << std::hex << ADDRINT(orgFuncptr) << ", " << std::dec << arg0 << ", " << std::hex << returnIp << ")" << std::endl << std::flush;

    // Call the relocated entry point of the original (replaced) routine.
    //
    VOID* v = orgFuncptr(arg0);

    return v;
}

// Pin calls this function every time a new img is loaded.
// It is best to do probe replacement when the image is loaded,
// because only one thread knows about the image at this time.
//
VOID ImageLoad(IMG img, VOID* v)
{
    // See if malloc() is present in the image.  If so, replace it.
    //
    RTN rtn = RTN_FindByName(img, "malloc");

    if (RTN_Valid(rtn))
    {
        if (RTN_IsSafeForProbedReplacement(rtn))
        {
            std::cout << "Replacing malloc in " << IMG_Name(img) << std::endl;

            // Define a function prototype that describes the application routine
            // that will be replaced.
            //
            PROTO proto_malloc = PROTO_Allocate(PIN_PARG(void*), CALLINGSTD_DEFAULT, "malloc", PIN_PARG(int), PIN_PARG_END());

            // Replace the application routine with the replacement function.
            // Additional arguments have been added to the replacement routine.
            //
            RTN_ReplaceSignatureProbed(rtn, AFUNPTR(NewMalloc), IARG_PROTOTYPE, proto_malloc, IARG_ORIG_FUNCPTR,
                                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_RETURN_IP, IARG_END);

        }
        else
        {
            std::cout << "Skip replacing malloc in " << IMG_Name(img) << " since it is not safe." << std::endl;
        }
    }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool demonstrates how to replace an original" << std::endl;
    std::cerr << " function with a custom function defined in the tool " << std::endl;
    std::cerr << " using probes.  The replacement function has a different " << std::endl;
    std::cerr << " signature from that of the original replaced function." << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* ===================================================================== */
/* Main: Initialize and start Pin in Probe mode.                         */
/* ===================================================================== */

int main(INT32 argc, CHAR* argv[])
{
    // Initialize symbol processing
    //
    PIN_InitSymbols();

    // Initialize pin
    //
    if (PIN_Init(argc, argv)) return Usage();

    // Register ImageLoad to be called when an image is loaded
    //
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Start the program in probe mode, never returns
    //
    PIN_StartProgramProbed();

    return 0;
}
