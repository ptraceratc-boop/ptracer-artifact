/*
 * Copyright (C) 2010-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  Print the first INS according to all syntax options.
 */

#include <iostream>
#include <fstream>
#include "pin.H"



BOOL test = true;

VOID Inst(INS ins, VOID* v)
{
    if (!test) return;
    test = false;

    std::cout << "DEFAULT: " << INS_Disassemble(ins) << std::endl;
    PIN_SetSyntaxATT();
    std::cout << "ATT: " << INS_Disassemble(ins) << std::endl;
    PIN_SetSyntaxIntel();
    std::cout << "INTEL: " << INS_Disassemble(ins) << std::endl;
    PIN_SetSyntaxXED();
    std::cout << "XED: " << INS_Disassemble(ins) << std::endl;
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    INS_AddInstrumentFunction(Inst, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
