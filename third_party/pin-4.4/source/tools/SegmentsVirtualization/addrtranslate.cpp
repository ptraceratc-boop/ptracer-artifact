/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <stdio.h>
#include <stdlib.h>

/*
 * This tool is used to translate the memory address operand of the first instruction after fnop.
 * It is used in the addrtranslate test coupled with movfs application.
 */

REG ScratchReg;

ADDRINT TranslateAddress(ADDRINT ea) { return ea + sizeof(ADDRINT); }

VOID Instruction(INS ins, VOID* v)
{
    static bool fnopFound = false;
    if (XED_ICLASS_FNOP == INS_Opcode(ins))
    {
        fnopFound = true;
        printf("fnop hit\n");
        return;
    }
    
    if (!fnopFound)
    {
        return;
    }

    UINT32 memops = INS_MemoryOperandCount(ins);

    printf("Number of memory operand: %d\n", memops);
    printf("Instruction being instrumented: %s\n", INS_Disassemble(ins).c_str());

    for (UINT32 i = 0; i < memops; i++)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(TranslateAddress), IARG_MEMORYOP_EA, i, IARG_RETURN_REGS, ScratchReg,
                       IARG_END);

        INS_RewriteMemoryOperand(ins, i, ScratchReg);
    }

    printf("fnop flag turned back to false\n");
    fnopFound = false;
}

INT32 Usage()
{
    return -1;
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    ScratchReg = PIN_ClaimToolRegister();
    INS_AddInstrumentFunction(Instruction, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
