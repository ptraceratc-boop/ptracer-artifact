/*
 * Copyright (C) 2009-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
  Insert a call to an analysis routine in probe mode.  From the analysis
  routine, call an application function using a function pointer.
*/

/* ===================================================================== */
#include "pin.H"
#include <iostream>
#include <stdlib.h>



/* ===================================================================== */

int myBlue(CONTEXT* ctxt, AFUNPTR pf_Blue, int one, int two)
{
    std::cout << " myBlue: Jitting Blue6() at address " << hexstr(ADDRINT(pf_Blue)) << std::endl;

    int res;

    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pf_Blue, NULL, PIN_PARG(int), &res, PIN_PARG(int), one,
                                PIN_PARG(int), two, PIN_PARG_END());

    std::cout << " myBlue: Returned from Blue6(); res = " << res << std::endl;

    return res;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        PROTO protoBlue =
            PROTO_Allocate(PIN_PARG(int), CALLINGSTD_DEFAULT, "Blue6", PIN_PARG(int), PIN_PARG(int), PIN_PARG_END());

        RTN blueRtn = RTN_FindByName(img, "Blue6");
        if (!RTN_Valid(blueRtn))
        {
            std::cout << "Blue6 cannot be found." << std::endl;
            exit(1);
        }

        RTN rtn = RTN_FindByName(img, "main");
        if (RTN_Valid(rtn))
        {
            RTN_Open(rtn);

            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
            {
                if (INS_IsCall(ins))
                {
                    INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(myBlue), IARG_PROTOTYPE, protoBlue, IARG_CONTEXT,
                                   IARG_ADDRINT, AFUNPTR(RTN_Address(blueRtn)), IARG_UINT32, 1, IARG_UINT32, 2, IARG_END);

                    std::cout << " Instrumenting " << RTN_Name(rtn) << " at address " << hexstr(INS_Address(ins)) << std::endl;
                }
            }

            RTN_Close(rtn);
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
