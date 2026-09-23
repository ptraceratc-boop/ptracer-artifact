/*
 * Copyright (C) 2008-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 * Insert a call before/after a function in probe mode.
 */

/* ===================================================================== */
#include "pin.H"
#if defined(TARGET_WINDOWS)
#include <windows/pinrt_windows.h>
#endif
#include <cstdlib>
#include <iostream>
#include "tool_macros.h"






/* ===================================================================== */
/* Analysis routines  */
/* ===================================================================== */

VOID Before(ADDRINT ebx, ADDRINT ebp, ADDRINT esp)
{
    std::cout << "Before:   ebx/r15 = " << std::hex << ebx << "  ebp/rbp = " << ebp << "  esp/rsp = " << esp << std::dec << std::endl;
}

VOID After(REG reg) { std::cout << "After: return value = " << reg << std::endl << std::flush; }

/* ===================================================================== */
/* Instrumentation routines  */
/* ===================================================================== */

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn = RTN_FindByName(img, C_MANGLE("Bar9"));
    if (RTN_Valid(rtn))
    {
        PROTO proto =
            PROTO_Allocate(PIN_PARG(int), CALLINGSTD_DEFAULT, "Bar9", PIN_PARG(int), PIN_PARG(int), PIN_PARG(int), PIN_PARG(int),
                           PIN_PARG(int), PIN_PARG(int), PIN_PARG(int), PIN_PARG(int), PIN_PARG(int), PIN_PARG_END());
        if (
#if defined(TARGET_IA32)
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before), IARG_PROTOTYPE, proto, IARG_REG_VALUE, LEVEL_BASE::REG_EBX,
                                 IARG_REG_VALUE, LEVEL_BASE::REG_EBP, IARG_REG_VALUE, LEVEL_BASE::REG_ESP, IARG_END) &&
#else
            RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before), IARG_PROTOTYPE, proto, IARG_REG_VALUE, LEVEL_BASE::REG_R15,
                                 IARG_REG_VALUE, LEVEL_BASE::REG_RBP, IARG_REG_VALUE, LEVEL_BASE::REG_RSP, IARG_END) &&
#endif
            RTN_InsertCallProbed(rtn, IPOINT_AFTER, AFUNPTR(After), IARG_PROTOTYPE, proto, IARG_REG_VALUE, REG_GAX, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
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
