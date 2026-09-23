/*
 * Copyright (C) 2022-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <errno.h>
#include <sys/syscall.h>




/* ================================================================== */
// Global variables
/* ================================================================== */

UINT32 tSysno[PIN_MAX_THREADS];
UINT32 tCount[PIN_MAX_THREADS];

std::ostream* out = &std::cerr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");

KNOB< BOOL > KnobCount(KNOB_MODE_WRITEONCE, "pintool", "count", "1",
                       "count instructions, basic blocks and threads in the application");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool prints out the number of dynamically executed " << std::endl
         << "instructions, basic blocks and threads in the application." << std::endl
         << std::endl;

    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

VOID SyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    tSysno[threadIndex] = PIN_GetSyscallNumber(ctxt, std);
}

VOID SyscallExit(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    if (tSysno[threadIndex] == __NR_getsid)
    {
        if (tCount[threadIndex] == 0)
        {
            PIN_SetSyscallReturn(ctxt, std, 0xDEADBEAF);
        }
        else if (tCount[threadIndex] == 1)
        {
            PIN_SetSyscallErrno(ctxt, std, -EINVAL);
        }
        tCount[threadIndex]++;
    }
}

VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    tSysno[threadid] = 0;
    tCount[threadid] = 0;
}

int main(int argc, char* argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::string fileName = KnobOutputFile.Value();

    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }
    PIN_AddThreadStartFunction(ThreadStart, NULL);
    PIN_AddSyscallEntryFunction(SyscallEntry, NULL);
    PIN_AddSyscallExitFunction(SyscallExit, NULL);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
