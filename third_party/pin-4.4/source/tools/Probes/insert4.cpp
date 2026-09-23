/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 * Insert a call before a function in probe mode.
 */

/* ===================================================================== */
#include "pin.H"
#include <iostream>
#include <stdlib.h>
#include "tool_macros.h"






/* ===================================================================== */
/* Analysis routines  */
/* ===================================================================== */

VOID Before_Malloc0(size_t size) { std::cout << "Before_Malloc0: Calling malloc() with size=" << size << std::endl << std::flush; }

VOID Before_Malloc1(size_t size) { std::cout << "Before_Malloc1: Calling malloc() with size=" << size << std::endl << std::flush; }

VOID Before_Free0(void* ptr) { std::cout << "Before_Free0: Calling free() with ptr=" << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush; }

VOID Before_Free1(void* ptr) { std::cout << "Before_Free1: Calling free() with ptr=" << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush; }

/* ===================================================================== */
/* Instrumentation routines  */
/* ===================================================================== */

VOID Sanity(IMG img, RTN rtn)
{
    if (PIN_IsProbeMode() && !RTN_IsSafeForProbedInsertion(rtn))
    {
        std::cout << "Cannot insert calls around " << RTN_Name(rtn) << "() in " << IMG_Name(img) << std::endl;
        exit(1);
    }
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn = RTN_FindByName(img, C_MANGLE("malloc"));
    if (RTN_Valid(rtn))
    {
        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Malloc0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Malloc1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting 2 calls before malloc in " << IMG_Name(img) << std::endl;
        }
        else
        {
            std::cout << "Cannot insert calls around " << RTN_Name(rtn) << "() in " << IMG_Name(img) << std::endl;
            exit(1);
        }
    }

    rtn = RTN_FindByName(img, C_MANGLE("free"));
    if (RTN_Valid(rtn))
    {
        Sanity(img, rtn);

        std::cout << "Inserting 2 calls before free in " << IMG_Name(img) << std::endl;

        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Free0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Free1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting 2 calls before malloc in " << IMG_Name(img) << std::endl;
        }
        else
        {
            std::cout << "Cannot insert calls around " << RTN_Name(rtn) << "() in " << IMG_Name(img) << std::endl;
            exit(1);
        }
    }
}

/* ===================================================================== */

int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
