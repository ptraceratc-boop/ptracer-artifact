/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This file contains an ISA-portable PIN tool that checks IARG_INST_PTR
 */

#include "pin.H"
#include <iostream>
#include <fstream>
using std::cerr;
using std::endl;
using std::string;

static INT32 Usage()
{
    cerr << "This tool check the IARG_INST_PTR parameter passed to a callback\n";
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

static VOID CheckIpAddress(VOID* ip, ADDRINT pc)
{
    ASSERTX(pc == (ADDRINT)ip);
}

VOID Instruction(INS ins, VOID* v)
{
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)CheckIpAddress, IARG_INST_PTR, IARG_ADDRINT, INS_Address(ins), IARG_END);
    if (INS_IsValidForIpointAfter(ins))
    {
        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)CheckIpAddress, IARG_INST_PTR, IARG_ADDRINT, INS_Address(ins), IARG_END);
    }
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    INS_AddInstrumentFunction(Instruction, 0);

    PIN_StartProgram();

    return 0;
}
