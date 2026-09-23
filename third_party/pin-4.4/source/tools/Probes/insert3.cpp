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

VOID Before_MyMalloc0(size_t size) { std::cout << "Before_MyMalloc0: Calling my_malloc() with size=" << size << std::endl << std::flush; }

VOID Before_MyMalloc1(size_t size) { std::cout << "Before_MyMalloc1: Calling my_malloc() with size=" << size << std::endl << std::flush; }

VOID Before_MyFree0(void* ptr)
{
    std::cout << "Before_MyFree0: Calling my_free() with ptr=" << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush;
}

VOID Before_MyFree1(void* ptr)
{
    std::cout << "Before_MyFree1: Calling my_free() with ptr=" << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush;
}

/* ===================================================================== */
/* Instrumentation routines  */
/* ===================================================================== */


/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img)) return;

    RTN rtn = RTN_FindByName(img, C_MANGLE("my_malloc"));
    if (RTN_Valid(rtn))
    {
        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_MyMalloc0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_MyMalloc1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting 2 calls before my_malloc in " << IMG_Name(img) << std::endl;
        }
        else
        {
            std::cout << "Cannot insert calls around " << RTN_Name(rtn) << "() in " << IMG_Name(img) << std::endl;
            exit(1);
        }
    }

    rtn = RTN_FindByName(img, C_MANGLE("my_free"));
    if (RTN_Valid(rtn))
    {
        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_MyFree0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_MyFree1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting 2 calls before my_free in " << IMG_Name(img) << std::endl;
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
