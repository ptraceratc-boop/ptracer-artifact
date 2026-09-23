/*
 * Copyright (C) 2006-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
  Replace an original function with a custom function defined in the tool. The
  new function can have either the same or different signature from that of its
  original function.
*/

/* ===================================================================== */
#include "pin.H"
#include <iostream>
#include <stdlib.h>



/* ===================================================================== */
static void (*pf_bar)(int);

/* ===================================================================== */
VOID Boo(CONTEXT* ctxt, AFUNPTR pf_Blue, int one)
{
    std::cout << "Jitting Blue1() with one arguments" << std::endl;

    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pf_Blue, NULL, PIN_PARG(void), PIN_PARG(int), one,
                                PIN_PARG_END());

    std::cout << "Returned from Blue1(); Native execution. " << std::endl;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "Bar1", PIN_PARG(int), PIN_PARG_END());

        VOID* pf_Blue;
        RTN rtn1 = RTN_FindByName(img, "Blue1");
        if (RTN_Valid(rtn1))
            pf_Blue = reinterpret_cast< VOID* >(RTN_Address(rtn1));
        else
        {
            std::cout << "Blue1 cannot be found." << std::endl;
            exit(1);
        }

        RTN rtn = RTN_FindByName(img, "Bar1");
        if (RTN_Valid(rtn))
        {
            std::cout << "Replacing " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;

            pf_bar = (void (*)(int))RTN_ReplaceSignature(rtn, AFUNPTR(Boo), IARG_PROTOTYPE, proto, IARG_CONTEXT, IARG_PTR,
                                                         pf_Blue, IARG_UINT32, 1, IARG_END);
        }
    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
