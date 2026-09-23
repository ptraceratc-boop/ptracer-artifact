/*
 * Copyright (C) 2016-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <execinfo.h>
#include <iostream>
#include <tool_macros.h>
#include "pin.H"




void qux_Before(const CONTEXT* ctxt)
{
    void* buf[128];
    PIN_LockClient();
    int nptrs = PIN_Backtrace(ctxt, buf, sizeof(buf) / sizeof(buf[0]));
    ASSERTX(nptrs > 0);
    char** bt = backtrace_symbols(buf, nptrs);
    PIN_UnlockClient();
    ASSERTX(NULL != bt);
    for (int i = 0; i < nptrs; i++)
    {
        std::cout << bt[i] << std::endl;
    }
    free(bt);
}

void InstImage(IMG img, void* v)
{
    if (IMG_IsMainExecutable(img))
    {
        RTN rtn = RTN_FindByName(img, C_MANGLE("qux"));
        ASSERTX(RTN_Valid(rtn));
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)qux_Before, IARG_CONST_CONTEXT, IARG_END);
        RTN_Close(rtn);
    }
}

int main(int argc, char** argv)
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        std::cerr << "usage..." << std::endl;
        return EXIT_FAILURE;
    }

    IMG_AddInstrumentFunction(InstImage, 0);

    PIN_StartProgram();
    return EXIT_FAILURE;
}
