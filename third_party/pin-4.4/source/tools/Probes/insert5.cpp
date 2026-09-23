/*
 * Copyright (C) 2008-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 * Insert a call before a function in probe mode.
 */

/* ===================================================================== */
#include "pin.H"
#include <cstdlib>
#include <iostream>
#include "tool_macros.h"






/* ===================================================================== */
/* Analysis routines  */
/* ===================================================================== */

VOID Before_Malloc(size_t size) { std::cout << "Before_Malloc: Calling malloc() with size=" << size << std::endl << std::flush; }

VOID After_Malloc(ADDRINT retval) { std::cout << "After_Malloc: malloc() return value=" << std::hex << retval << std::dec << std::endl << std::flush; }

VOID Before_Free(void* ptr) { std::cout << "Before_Free: Calling free() with ptr=" << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush; }

VOID After_Free() { std::cout << "After_Free: returning from free()." << std::endl << std::flush; }

/* ===================================================================== */
/* Instrumentation routines  */
/* ===================================================================== */

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn = RTN_FindByName(img, C_MANGLE("malloc"));
    if (RTN_Valid(rtn))
    {
        PROTO proto_malloc = PROTO_Allocate(PIN_PARG(void*), CALLINGSTD_DEFAULT, "malloc", PIN_PARG(size_t), PIN_PARG_END());

        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Malloc),
                                 /*IARG_PROTOTYPE, proto_malloc,*/
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_AFTER, AFUNPTR(After_Malloc), IARG_PROTOTYPE, proto_malloc, IARG_REG_VALUE, REG_GAX,
                                 IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting calls before/after malloc in " << IMG_Name(img) << std::endl;
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
        PROTO proto_free = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "free", PIN_PARG(void*), PIN_PARG_END());

        if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_Free),
                                 /*IARG_PROTOTYPE, proto_free,*/
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END) &&
            RTN_InsertCallProbed(rtn, IPOINT_AFTER, AFUNPTR(After_Free), IARG_PROTOTYPE, proto_free, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            std::cout << "Inserting calls before/after free in " << IMG_Name(img) << std::endl;
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
