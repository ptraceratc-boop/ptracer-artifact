/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

// The running count of instructions is kept here
// make it static to help the compiler optimize docount
static UINT64 icount = 0;

// This function is called before every instruction is executed
VOID docount() { icount++; }

// Pin calls this function every time a new instruction is encountered
VOID Instruction(INS ins, VOID* v)
{
    // Insert a call to docount before every instruction, no arguments are passed
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);
}

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    fprintf(stdout, "pid before exec child: %u\n", getpid());
    fprintf(stdout, "first app inscount: %" PRIu64 "\n", icount);
    return TRUE;
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID* v) { fprintf(stdout, "second app inscount: %" PRIu64 "\nJIT Fini called\n", icount); }

UINT32 lastSyscall = (UINT32)(-1);

VOID SyscallBefore(THREADID tid, CONTEXT* ctxt, SYSCALL_STANDARD scStd, VOID* arg)
{
    lastSyscall = (UINT32)PIN_GetSyscallNumber(ctxt, scStd);
    if (lastSyscall == SYS_execve || lastSyscall == SYS_execveat)
    {
        LOG("Before SYS_execve/SYS_execveat syscall\n");
    }
}

VOID SyscallAfter(THREADID tid, CONTEXT* ctxt, SYSCALL_STANDARD scStd, VOID* arg)
{
    if (lastSyscall == SYS_execve || lastSyscall == SYS_execveat)
    {
        WARNING("Error - After SYS_execve/SYS_execveat syscall\n");
    }
}

/* ===================================================================== */

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    LOG("Starting execveat_jit_tool\n");

    // Register Instruction to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    PIN_AddSyscallEntryFunction(SyscallBefore, 0);
    PIN_AddSyscallExitFunction(SyscallAfter, 0);

    PIN_StartProgram();

    return 0;
}
