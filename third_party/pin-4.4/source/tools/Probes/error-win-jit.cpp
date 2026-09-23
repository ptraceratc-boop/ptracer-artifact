/*
 * Copyright (C) 2010-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool demonstrates how to get the value of the application's
// error code on windows in jit mode.
//

#include "pin.H"
#include <iostream>
#include <stdlib.h>
#include <errno.h>
#include <windows/pinrt_windows.h>





AFUNPTR pfnGetLastError = 0;

/* ===================================================================== */
VOID ToolCheckError(CONTEXT* ctxt)
{
    unsigned long err_code;

    if (pfnGetLastError != 0)
    {
        std::cerr << "Tool: calling GetLastError()" << std::endl;

        PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pfnGetLastError, NULL, PIN_PARG(unsigned long),
                                    &err_code, PIN_PARG_END());

        std::cerr << "Tool: error code=" << err_code << std::endl;
    }
    else
        std::cerr << "Tool: GetLastError() not found." << std::endl;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "CheckError", PIN_PARG_END());

        RTN rtn = RTN_FindByName(img, "CheckError");
        if (RTN_Valid(rtn))
        {
            std::cout << "Replacing " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;

            RTN_ReplaceSignature(rtn, AFUNPTR(ToolCheckError), IARG_PROTOTYPE, proto, IARG_CONTEXT, IARG_END);
        }
    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    pfnGetLastError = (AFUNPTR)WINDOWS::GetProcAddress(WINDOWS::GetModuleHandle("kernel32.dll"), "GetLastError");

    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
