/*
 * Copyright (C) 2011-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// This tool is used to test performance affects of inserting a call in probe mode

#include <stdio.h>
#include "pin.H"


VOID AtRtn() {}

VOID Image(IMG img, VOID* v)
{
    if ((IMG_Name(img).find("ntdll.dll") != std::string::npos) || (IMG_Name(img).find("NTDLL.DLL") != std::string::npos) ||
        (IMG_Name(img).find("NTDLL.dll") != std::string::npos))

    {
        return;
    }
    if ((IMG_Name(img).find("MSVCR") != std::string::npos) || (IMG_Name(img).find("msvcr") != std::string::npos))

    { // _NLG_Return2 causes problems
        return;
    }

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            if (RTN_Name(rtn).find(".text") != std::string::npos)
            {
                continue;
            }

            if (RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(AtRtn), IARG_END))
            {
                ASSERTX(RTN_IsSafeForProbedInsertion(rtn));
            }
        }
    }
}

int main(int argc, char** argv)
{
    PIN_Init(argc, argv);
    PIN_InitSymbols();

    IMG_AddInstrumentFunction(Image, 0);

    PIN_StartProgramProbed();
    return 0;
}
