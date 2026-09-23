/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * A test for callbacks around fork in jit mode.
 */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "pin.H"

#include <iostream>
#include <fstream>



INT32 Usage()
{
    std::cerr << "This pin tool registers callbacks around fork().\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

pid_t parent_pid;
PIN_LOCK pinLock;

VOID BeforeFork(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    std::cerr << "TOOL: Before fork." << std::endl;
    PIN_ReleaseLock(&pinLock);
    parent_pid = PIN_GetPid();
}

VOID AfterForkInParent(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    std::cerr << "TOOL: After fork in parent." << std::endl;
    PIN_ReleaseLock(&pinLock);

    if (PIN_GetPid() != parent_pid)
    {
        std::cerr << "PIN_GetPid() fails in parent process" << std::endl;
        exit(-1);
    }
}

VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    std::cerr << "TOOL: After fork in child." << std::endl;
    PIN_ReleaseLock(&pinLock);

    if ((PIN_GetPid() == parent_pid) || (getppid() != parent_pid))
    {
        std::cerr << "PIN_GetPid() fails in child process" << std::endl;
        exit(-1);
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Initialize the pin lock
    PIN_InitLock(&pinLock);

    // Register a notification handler that is called when the application
    // forks a new process.
    PIN_AddForkFunction(FPOINT_BEFORE, BeforeFork, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, 0);
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
