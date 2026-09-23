/*
 * Copyright (C) 2009-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * A test for callbacks around fork in jit mode.
 * The test checks that
 *   - callbacks before/after fork are called
 *   - context in after-fork callback includes the correct child pid
 */
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <syscall.h>

#include "pin.H"

#include <iostream>
#include <fstream>





/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "fork_jit_tool.out", "specify file name");

std::ofstream Out;
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool tests probe replacement.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

pid_t childPid = 0;
PIN_LOCK pinLock;
std::ofstream childOut;

/*
 * To make sure that before-fork callback works
 */
VOID BeforeFork(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    PIN_GetLock(&pinLock, threadid + 1);
    Out << "TOOL: Before fork." << std::endl;
    PIN_ReleaseLock(&pinLock);
}

/*
 * To make sure that after-fork callback works
 * and
 * the context has the correct child pid in syscall-return register.
 * The child pid value should be equal in after-fork and after-syscall
 * callbacks.
 */
VOID AfterForkInParent(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    pid_t parentPid = *(pid_t*)&arg;
    PIN_GetLock(&pinLock, threadid + 1);
    Out << "TOOL: After fork in parent." << std::endl;
    PIN_ReleaseLock(&pinLock);
    if (PIN_GetPid() != parentPid)
    {
        std::cerr << "PIN_GetPid() fails in parent process" << std::endl;
        exit(-1);
    }
    else
    {
        Out << "PIN_GetPid() is correct in parent process" << std::endl;
    }

#ifdef TARGET_BSD
    SYSCALL_STANDARD syscallStd = SYSCALL_STANDARD_IA32E_BSD;
#else
#if defined(TARGET_IA32E)
    SYSCALL_STANDARD syscallStd = SYSCALL_STANDARD_IA32E_LINUX;
#else
    SYSCALL_STANDARD syscallStd = SYSCALL_STANDARD_IA32_LINUX;
#endif
#endif

    pid_t afterForkChildPid = (pid_t)PIN_GetSyscallReturn(ctxt, syscallStd);
    if (!childPid)
    {
        childPid = afterForkChildPid;
    }
    else if (childPid != afterForkChildPid)
    {
        std::cerr << "Child pid received in syscall-after callback " << childPid << " and child Pid in after-fork callback "
             << afterForkChildPid << " don't match " << std::endl;
        exit(-1);
    }
}

VOID OpenChildOutput()
{
    if (!childOut.is_open())
    {
        char* outFileName = new char[KnobOutputFile.Value().size() + 10];
        sprintf(outFileName, "%s_%d", KnobOutputFile.Value().c_str(), PIN_GetPid());
        childOut.open(outFileName);
    }
}

VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    // After the fork, there is only one thread in the child process.  It's possible
    // that a different thread in the parent held this lock when the fork() happened.
    // Since that thread does not exist in the child, it will never release the lock.
    // Compensate by re-initializing the lock here in the child.

    PIN_GetLock(&pinLock, threadid + 1);
    PIN_ReleaseLock(&pinLock);

    pid_t parentPid = *(pid_t*)&arg;

    OpenChildOutput();

    childOut << "TOOL: After fork in child." << std::endl;

    pid_t currentPid = PIN_GetPid();

    if ((currentPid == parentPid) || (getppid() != parentPid))
    {
        std::cerr << "PIN_GetPid() fails in child process" << std::endl;
        exit(-1);
    }
    else
    {
        childOut << "PIN_GetPid() is correct in child process" << std::endl;
    }
    childOut << "Child pid " << currentPid << std::endl;
}

UINT32 lastSyscall = (UINT32)(-1);

VOID SyscallBefore(THREADID tid, CONTEXT* ctxt, SYSCALL_STANDARD scStd, VOID* arg)
{
    lastSyscall = (UINT32)PIN_GetSyscallNumber(ctxt, scStd);
}

VOID SyscallAfter(THREADID tid, CONTEXT* ctxt, SYSCALL_STANDARD scStd, VOID* arg)
{
    pid_t parentPid  = *(pid_t*)&arg;
    pid_t currentPid = PIN_GetPid();
    if ((
#if defined(TARGET_BSD)
            (lastSyscall == SYS_fork)
#else
            (lastSyscall == SYS_fork) || (lastSyscall == SYS_clone)
#endif
                ) &&
        (parentPid == currentPid))
    {
        //We are looking at ater-fork in parent
        pid_t res = PIN_GetSyscallReturn(ctxt, scStd);
        if (childPid)
        {
            if (res != childPid)
            {
                std::cerr << "Child pid received in after fork callback " << childPid << " and child Pid in syscall-after callback "
                     << res << " don't match " << std::endl;
                exit(-1);
            }
        }
    }
}

int main(INT32 argc, CHAR** argv)
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    PIN_InitLock(&pinLock);

    Out.open(KnobOutputFile.Value().c_str());

    unsigned long parentPid = (unsigned long)PIN_GetPid();
    PIN_AddForkFunction(FPOINT_BEFORE, BeforeFork, (VOID*)parentPid);
    PIN_AddForkFunction(FPOINT_AFTER_IN_PARENT, AfterForkInParent, (VOID*)parentPid);
    PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, (VOID*)parentPid);
    PIN_AddSyscallEntryFunction(SyscallBefore, 0);
    PIN_AddSyscallExitFunction(SyscallAfter, (VOID*)parentPid);
    // Never returns
    PIN_StartProgram();

    return 0;
}
