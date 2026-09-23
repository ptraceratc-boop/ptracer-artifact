/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <fstream>



UINT64 nops = 0;
std::ofstream* out;
VOID Fini(INT32 code, VOID* v)
{
    *out << "NOPS: " << nops << std::endl;
    out->close();
}

VOID Instruction(INS ins, VOID* v)
{
    if (INS_IsNop(ins))
    {
        *out << INS_Disassemble(ins) << std::endl;
        nops++;
    }
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);
    out = new std::ofstream("nops.out");

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
