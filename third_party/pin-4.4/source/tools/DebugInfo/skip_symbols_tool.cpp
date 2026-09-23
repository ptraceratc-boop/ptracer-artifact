/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>

/* ================================================================== */
// Global variables
/* ================================================================== */

KNOB< BOOL > KnobSkipMain(KNOB_MODE_WRITEONCE, "pintool", "skip_main", "0", "Skip symbol processing for the main executable");

/* ================================================================== */
// Callbacks
/* ================================================================== */

BOOL OnProcessSymbols(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img) && KnobSkipMain)
    {
        return FALSE;
    }
    return TRUE;
}

VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        // Try to find the static function "MyStaticFunction"
        RTN rtn = RTN_FindByName(img, "MyStaticFunction");

        if (!RTN_Valid(rtn))
        {
            // Try with underscore prefix for Windows or some systems
            rtn = RTN_FindByName(img, "_MyStaticFunction");
        }

        if (KnobSkipMain)
        {
            if (RTN_Valid(rtn))
            {
                std::cerr << "Error: Found MyStaticFunction when symbols should be skipped." << std::endl;
                PIN_ExitProcess(1);
            }
        }
        else
        {
            if (!RTN_Valid(rtn))
            {
                std::cerr << "Error: Could not find MyStaticFunction when symbols should be loaded." << std::endl;
                PIN_ExitProcess(1);
            }
        }
    }
}

/* ================================================================== */
// Main
/* ================================================================== */

int main(int argc, char* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return 1;
    }

    IMG_AddPreDebugInfoProcessCallback(OnProcessSymbols, 0);
    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();
    return 0;
}
