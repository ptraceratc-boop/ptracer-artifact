/*
 * Copyright (C) 2004-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <fstream>



VOID PrintArgs(INT32 arg1, INT32 arg2, INT32 arg3, INT32 arg4, INT32 arg5, INT32 arg6, INT32 arg7, INT32 arg8)
{
    std::ofstream out("args.output");
    out << arg1 << std::endl;
    out << arg2 << std::endl;
    out << arg3 << std::endl;
    out << arg4 << std::endl;
    out << arg5 << std::endl;
    out << arg6 << std::endl;
    out << arg7 << std::endl;
    out << arg8 << std::endl;
}

VOID CheckIp(ADDRINT ip1, ADDRINT ip2, ADDRINT ip3, ADDRINT ip4, ADDRINT ip5, ADDRINT ip6, ADDRINT ip7, ADDRINT ip8, ADDRINT ip9,
             ADDRINT ip10)
{
    if (ip1 != ip2) fprintf(stderr, "ip1 %p ip2 %p\n", (void*)ip1, (void*)ip2);

    ASSERTX(ip1 == ip2);
    ASSERTX(ip1 == ip3);
    ASSERTX(ip1 == ip4);
    ASSERTX(ip1 == ip5);
    ASSERTX(ip1 == ip6);
    ASSERTX(ip1 == ip7);
    ASSERTX(ip1 == ip8);
    ASSERTX(ip1 == ip9);
    ASSERTX(ip1 == ip10);
}

VOID PIN_FAST_ANALYSIS_CALL CheckIpFast(ADDRINT ip1, ADDRINT ip2, ADDRINT ip3, ADDRINT ip4, ADDRINT ip5, ADDRINT ip6, ADDRINT ip7,
                                        ADDRINT ip8, ADDRINT ip9, ADDRINT ip10)
{
    CheckIp(ip1, ip2, ip3, ip4, ip5, ip6, ip7, ip8, ip9, ip10);
}

VOID Instruction(INS ins, VOID* v)
{
    static INT32 count = 0;

    if (count % 64 == 0)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(CheckIp), IARG_INST_PTR, IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_END);
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(CheckIpFast), IARG_FAST_ANALYSIS_CALL, IARG_INST_PTR, IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT,
                       INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_ADDRINT, INS_Address(ins), IARG_END);
    }

    if (count == 0)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(PrintArgs), IARG_UINT32, 1, IARG_UINT32, 2, IARG_UINT32, 3, IARG_UINT32,
                       4, IARG_UINT32, 5, IARG_UINT32, 6, IARG_UINT32, 7, IARG_UINT32, 8, IARG_END);
    }

    count++;
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    INS_AddInstrumentFunction(Instruction, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
