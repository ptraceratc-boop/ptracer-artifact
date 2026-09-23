/*
 * Copyright (C) 2010-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool demonstrates how to get the value of the application's
// error code on windows in probe mode.
//

#include "pin.H"
#include <iostream>
#include <stdlib.h>
#include <errno.h>




#include <windows/pinrt_windows.h>

typedef ADDRINT (*GET_LAST_ERROR_FUNPTR)();

// Address of the GetLastError API.
ADDRINT pfnGetLastError = 0;

/* ===================================================================== */
VOID ToolCheckError()
{
    if (pfnGetLastError != 0)
    {
        ADDRINT err_code = (*(GET_LAST_ERROR_FUNPTR)pfnGetLastError)();

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
            RTN_ReplaceSignatureProbed(rtn, AFUNPTR(ToolCheckError), IARG_PROTOTYPE, proto, IARG_END);
        }
    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    pfnGetLastError = (ADDRINT)WINDOWS::GetProcAddress(WINDOWS::GetModuleHandle("kernel32.dll"), "GetLastError");
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
