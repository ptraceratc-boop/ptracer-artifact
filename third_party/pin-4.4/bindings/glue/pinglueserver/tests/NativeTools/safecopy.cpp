/*
 * Copyright (C) 2005-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "pin.H"
#include <iostream>
#include <fstream>
using std::cerr;
using std::endl;

std::ofstream* out = 0;

//=======================================================
//  Analysis routines
//=======================================================

// Move from memory to register
ADDRINT DoLoad(REG reg, ADDRINT* addr)
{
    *out << "Emulate loading from addr " << addr << " to " << REG_StringShort(reg) << endl;
    ADDRINT value;
    PIN_SafeCopy(&value, addr, sizeof(ADDRINT));
    return value;
}

//=======================================================
// Instrumentation routines
//=======================================================

VOID EmulateLoad(INS ins, VOID* v)
{
    // Find the instructions that move a value from memory to a register
    if (INS_Opcode(ins) == XED_ICLASS_MOV && INS_IsMemoryRead(ins) && INS_OperandIsReg(ins, 0) && INS_OperandIsMemory(ins, 1))
    {
        // op0 <- *op1
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(DoLoad), IARG_UINT32, REG(INS_OperandReg(ins, 0)), IARG_MEMORYREAD_EA,
                       IARG_RETURN_REGS, INS_OperandReg(ins, 0), IARG_END);

        // Delete the instruction
        INS_Delete(ins);
    }
}

static VOID Fini(INT32 code, VOID* v)
{
    // Test PIN_SafeCopyEx
    *out << "TOOL: Generate pin exception" << endl;
    VOID* to      = (VOID*)0;
    ADDRINT* from = 0;
    EXCEPTION_INFO exptInfo;
    size_t toCopy = 1024;
    if (PIN_SafeCopyEx(to, from, toCopy, &exptInfo) != toCopy)
    {
        if (PIN_GetExceptionCode(&exptInfo) != EXCEPTCODE_ACCESS_INVALID_ADDRESS)
        {
            *out << "TOOL ERROR: PIN_SafeCopyEx returned with an unexpected exception code" << endl;
            *out << PIN_ExceptionToString(&exptInfo) << endl;
            return;
        }
        ADDRINT exptAddr = PIN_GetExceptionAddress(&exptInfo);
        if (exptAddr != 0)
        {
            *out << "TOOL ERROR: Exception from PIN_SafeCopyEx returned with non NULL address" << endl;
            return;
        }
        else
        {
            *out << "TOOL: PIN_SafeCopyEx failed as expected" << endl;
            *out << PIN_ExceptionToString(&exptInfo) << endl;
        }
    }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool demonstrates the use of SafeCopy" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Write to a file since cout and cerr maybe closed by the application
    out = new std::ofstream("safecopy.out");

    // Initialize pin & symbol manager
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();

    // Register EmulateLoad to be called to instrument instructions
    INS_AddInstrumentFunction(EmulateLoad, 0);

    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    return 0;
}
