/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <chrono>
#include <unistd.h>
#include <syscall.h>
#include "rpc_schema.h"

/*
 * This pintool send an RPC request (OS_Syscall(SYS_rsc_do_rpc)) before and after fork system call (from both parent and child).
 * The return value of this RPC request is the pid of the remote process (pind).
 * The pintool prints the pid from which the request was sent and the return value (the remoote pid).
 * We analyze these printouts to verify that a forked process interacts with a separate pind process.
 */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output file name");
std::ostream* OUT = &std::cerr;

#define PIDSTR "[" << std::dec << getpid() << "] "

void SendRequest()
{
    uint32_t arg0 = 123;
    uint64_t ret  = 0;
    if (remote::do_rpc< TEST_SCHEMA >(ret, arg0))
    {
        *OUT << PIDSTR << "SUCCESS: remote::do_rpc< TEST_SCHEMA >() with rpcId=" << std::dec << TEST_SCHEMA.rpcId << " ret "
             << ret << std::endl;
    }
    else
    {
        *OUT << PIDSTR << "FAIL: remote::do_rpc< TEST_SCHEMA >() with rpcId=" << std::dec << TEST_SCHEMA.rpcId << std::endl;
        PIN_ExitApplication(1);
    }

    OUT->flush();
}

VOID Fini(INT32 code, VOID* v) { std::cout << PIDSTR << "Fini" << std::endl; }

void BeforeFork(THREADID threadid, const CONTEXT* ctxt, VOID* v)
{
    std::cout << PIDSTR << "Before fork in parent" << std::endl;
    SendRequest();
}

void AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* v)
{
    std::cout << PIDSTR << "After fork in child" << std::endl;
    SendRequest();
}

void AfterForkInParent(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    std::cout << PIDSTR << "After fork in parent" << std::endl;
    SendRequest();
}

void BeforeForkProbed(UINT32 childPid, void* data)
{
    std::cout << PIDSTR << "Before fork in parent" << std::endl;
    SendRequest();
}

void AfterForkInParentProbed(UINT32 childPid, void* data)
{
    std::cout << PIDSTR << "After fork in parent" << std::endl;
    SendRequest();
}

void AfterForkInChildProbed(UINT32 childPid, void* data)
{
    std::cout << PIDSTR << "After fork in child" << std::endl;
    SendRequest();
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        std::cout << "PIN_Init() failed" << std::endl;
        return -1;
    }

    std::string fileName = KnobOutputFile.Value();
    if (!fileName.empty())
    {
        OUT = new std::ofstream(fileName.c_str());
    }

    if (PIN_IsProbeMode())
    {
        PIN_AddForkFunctionProbed(FPOINT_BEFORE, BeforeForkProbed, 0);
        PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_CHILD, AfterForkInChildProbed, 0);
        PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_PARENT, AfterForkInParentProbed, 0);
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_AddFiniFunction(Fini, 0);
        PIN_AddForkFunction(FPOINT_BEFORE, BeforeFork, 0);
        PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, 0);
        PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, 0);
        PIN_StartProgram();
    }
    return 0;
}
