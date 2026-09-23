/*
 * Copyright (C) 2011-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <map>
#include <utility>





int howmany    = 2;
bool continue_ = true;
VOID Routine(RTN rtn, void* v)
{
    if (continue_)
    {
        RTN_Open(rtn);
        if (strcmp(RTN_Name(rtn).c_str(), "main") == 0)
        {
            std::cout << "RTN = " << RTN_Name(rtn) << std::endl;
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins) && continue_; ins = INS_Next(ins))
            {
                if (howmany > 0)
                {
                    std::cout << ": " << INS_Disassemble(ins) << std::endl;
                    --howmany;
                }
                else
                    continue_ = false;
            }
        }
        RTN_Close(rtn);
    }
}

VOID Image(IMG img, void* v)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec) && continue_; sec = SEC_Next(sec))
    {
        if (SEC_IsExecutable(sec))
        {
            for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn) && continue_; rtn = RTN_Next(rtn))
                Routine(rtn, v);
        }
    }
}

int main(int argc, char** argv)
{
    KNOB< std::string > KnobSetSyntax(KNOB_MODE_WRITEONCE, "pintool", "setsyntax", "3", "specify disassem syntax");

    if (PIN_Init(argc, argv))
    {
        std::cerr << "Cannot init PIN" << std::endl;
        return 1;
    }

    int setSyntax = atoi(KnobSetSyntax.Value().c_str());
    switch (setSyntax)
    {
        case 0:
            PIN_SetSyntaxIntel();
            break;
        case 1:
            PIN_SetSyntaxATT();
            break;
        case 2:
            PIN_SetSyntaxXED();
            break;
    }
    PIN_InitSymbols();

    IMG_AddInstrumentFunction(Image, 0);

    PIN_StartProgram();
    return 0;
}
