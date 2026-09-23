/*
 * Copyright (C) 2012-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Probe mode detach-reattach test which confirms that each thread get its respective thread detach callbacks
 * in a scenario that one of the application threads (main or secondary) may be a zombie thread.
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <assert.h>
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "zombie_utils.h"



#define MAX_ITERATION 5

pid_t zombiePid = -1;

TEST_TYPE testType = TEST_TYPE_DEFAULT;

PIN_LOCK pinLock;



/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name");

std::ofstream traceFile;

/* ===================================================================== */

// Session control checks that there is no callbacks mix between different
// attach-detach iterations
class SESSION_CONTROL
{
  public:
    SESSION_CONTROL() : _currentIteration(0), _threadCounter(0), _startAttachSession(FALSE), _startDetachSession(FALSE) {}

    static VOID ApplicationStart(VOID* v);
    static VOID AttachedThreadStart(VOID* sigmask, VOID* v);
    static VOID DedicatedThread(VOID* arg);

    VOID StartIteration(UINT32 it)
    {
        _currentIteration = it;
        _threadCounter    = 0;
    }
    UINT32 CurrentIteration() { return _currentIteration; }
    BOOL GotFirstThreadNotification(UINT32 it) { return ((it == _currentIteration) && (_threadCounter > 0)); }
    static SESSION_CONTROL* Instance() { return &m_instance; }
    VOID StartDetach()
    {
        _startDetachSession = TRUE;
        _startAttachSession = FALSE;
    }
    VOID StartAttach()
    {
        _startAttachSession = TRUE;
        _startDetachSession = FALSE;
    }
    VOID WaitForDetach()
    {
        while (!_startDetachSession)
            sched_yield();
    }
    VOID WaitForAttach()
    {
        while (!_startAttachSession)
            sched_yield();
    }

  private:
    UINT32 _currentIteration;
    UINT32 _threadCounter;
    volatile BOOL _startAttachSession;
    volatile BOOL _startDetachSession;
    static SESSION_CONTROL m_instance;
};

SESSION_CONTROL SESSION_CONTROL::m_instance;

SESSION_CONTROL* SessionControl() { return SESSION_CONTROL::Instance(); }

// Detach session.
// Callbacks and function replacements.
class DETACH_SESSION
{
  public:
    // Detach completion notification
    static VOID DetachCompleted(VOID* v);
    static VOID DetachThreadStart(VOID* v);
    static VOID ImageLoad(IMG img, VOID* v);
};

// Reattach session
class REATTACH_SESSION
{
  public:
    static VOID ImageLoad(IMG img, VOID* v);
    static VOID AttachedThreadStart(VOID* sigmask, VOID* v);
};

// Pin-tool detach-completed callback
// Called from Pin
VOID DETACH_SESSION::DetachCompleted(VOID* v)
{
    unsigned long detachIteration = (unsigned long)v;
    if (detachIteration != SessionControl()->CurrentIteration())
    {
        std::cerr << "Detach iteration error: Expected " << SessionControl()->CurrentIteration() << " Rececived " << detachIteration
             << " In DetachCompleted" << std::endl;
        PIN_ExitProcess(1);
    }

    PIN_GetLock(&pinLock, PIN_GetTid());
    traceFile << "Detach session " << detachIteration << " Detach completed; tid = " << PIN_GetTid() << std::endl;
    if (detachIteration == MAX_ITERATION)
    {
        traceFile << "TEST PASSED" << std::endl;
        traceFile.close();
        PIN_ExitProcess(0);
    }
    PIN_ReleaseLock(&pinLock);
    SessionControl()->StartAttach();
}

// Notify the application when Pin attaches to it.
BOOL AfterAttachProbe(void) { return TRUE; }

// Sets the pid of the zombie thread.
VOID SetZombiePid(pid_t pid) { zombiePid = pid; }

// Set the type of the thread, meaning:
// 1. Which thread (main/secondary) is a zombie thread.
// 2. Is the main thread turn into zombie only not in the first attach session.
VOID SetTestType(TEST_TYPE exprType) { testType = exprType; }

VOID AttachMain(VOID* arg)
{
    UINT32 reattachIteration = *(reinterpret_cast< UINT32* >(&arg));
    SessionControl()->StartIteration(reattachIteration);
    IMG_AddInstrumentFunction(REATTACH_SESSION::ImageLoad, arg);
    PIN_AddDetachFunctionProbed(DETACH_SESSION::DetachCompleted, arg);
    PIN_AddThreadAttachProbedFunction(SESSION_CONTROL::AttachedThreadStart, arg);
    PIN_AddThreadDetachProbedFunction(DETACH_SESSION::DetachThreadStart, arg);
}

// This is the start function which is called by the child process of the main process
// at the beginning of its execution.
// Its performs the repeated detach-reattach.
VOID SESSION_CONTROL::DedicatedThread(VOID* arg)
{
    static ADDRINT reattachIteration = 2;
    while (1)
    {
        // Wait until the testType is set by the application.
        while (testType == TEST_TYPE_DEFAULT)
            sleep(1);
        {
            // If the main thread will turn into zombie before the first reattach session.
            if (testType == TEST_TYPE_MAIN_THREAD_ZOMBIE_IN_REATTACH)
            {
                // Wait until the tool records the pid of the main application thread,
                while (zombiePid == -1)
                    sleep(1);
            }
        }

        SessionControl()->WaitForDetach();
        PIN_GetLock(&pinLock, PIN_GetTid());
        traceFile << "Pin tool: sending detach request" << std::endl;
        PIN_ReleaseLock(&pinLock);
        PIN_DetachProbed();
        SessionControl()->WaitForAttach();
        PIN_GetLock(&pinLock, PIN_GetTid());
        traceFile << "Pin tool: sending attach request" << std::endl;
        PIN_ReleaseLock(&pinLock);

        // If the main thread will turn into zombie before the first time it reattaches to the application.
        if (testType == TEST_TYPE_MAIN_THREAD_ZOMBIE_IN_REATTACH)
        {
            // Wait until the main thread turns into a zombie thread.
            while (!isZombie(zombiePid))
                sleep(1);
        }

        PIN_AttachProbed(AttachMain, (VOID*)reattachIteration++);
    }
}

// Thread start notification in the first session
VOID SESSION_CONTROL::AttachedThreadStart(VOID* sigmask, VOID* arg)
{
    unsigned long iteration = (unsigned long)arg;
    if (iteration != SessionControl()->CurrentIteration())
    {
        std::cerr << "Iteration error: Expected " << SessionControl()->CurrentIteration() << " Received " << iteration
             << " In AttachedThreadStart" << std::endl;
        PIN_ExitProcess(1);
    }
    PIN_GetLock(&pinLock, PIN_GetTid());
    ++(SessionControl()->_threadCounter);
    traceFile << "Thread start "
              << " notification at session " << iteration << " tid " << PIN_GetTid() << std::endl;

    // Call StartDetach once per iteration on the first start thread callback.
    // When AttachedThreadStart is called it means application is already started to run after attach.
    if (1 == SessionControl()->_threadCounter)
    {
        SessionControl()->StartDetach();
    }
    PIN_ReleaseLock(&pinLock);
}

VOID DETACH_SESSION::DetachThreadStart(VOID* arg)
{
    unsigned long iteration = (unsigned long)arg;
    if (iteration != SessionControl()->CurrentIteration())
    {
        std::cerr << "Iteration error: Expected " << SessionControl()->CurrentIteration() << " Received " << iteration
             << " In DetachThreadStart" << std::endl;
        PIN_ExitProcess(1);
    }
    PIN_GetLock(&pinLock, PIN_GetTid());
    traceFile << "Thread detach "
              << " notification at session " << iteration << " tid " << PIN_GetTid() << std::endl;
    PIN_ReleaseLock(&pinLock);
}

// Image load callback for the first Pin session
VOID DETACH_SESSION::ImageLoad(IMG img, VOID* arg)
{
    if (IMG_IsMainExecutable(img))
    {
        RTN rtn = RTN_FindByName(img, "AfterAttach1");

        // Relevant only in the attach scenario.
        if (RTN_Valid(rtn))
        {
            RTN_ReplaceProbed(rtn, AFUNPTR(AfterAttachProbe));
        }

        rtn = RTN_FindByName(img, "NotifyZombiePid");

        // Relevant only in the attach scenario.
        if (RTN_Valid(rtn))
        {
            ASSERTX(RTN_InsertCallProbed(rtn, IPOINT_BEFORE, (AFUNPTR)SetZombiePid, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END));
        }

        rtn = RTN_FindByName(img, "NotifyTestType");

        // Relevant only in the attach scenario.
        if (RTN_Valid(rtn))
        {
            ASSERTX(RTN_InsertCallProbed(rtn, IPOINT_BEFORE, (AFUNPTR)SetTestType, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END));
        }
    }

    PIN_GetLock(&pinLock, PIN_GetTid());
    traceFile << "Load image " << IMG_Name(img) << " at session " << (unsigned long)arg << std::endl;
    PIN_ReleaseLock(&pinLock);
}

// Image load callback for the second Pin session
VOID REATTACH_SESSION::ImageLoad(IMG img, VOID* arg)
{
    PIN_GetLock(&pinLock, PIN_GetTid());
    traceFile << "Load image " << IMG_Name(img) << " at session " << (unsigned long)arg << std::endl;
    PIN_ReleaseLock(&pinLock);
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);
    SessionControl()->StartIteration(1);
    PIN_InitLock(&pinLock);
    traceFile.open(knobOutputFile.Value().c_str());
    IMG_AddInstrumentFunction(DETACH_SESSION::ImageLoad, (VOID*)1);
    PIN_AddDetachFunctionProbed(DETACH_SESSION::DetachCompleted, (VOID*)1);
    PIN_AddThreadAttachProbedFunction(SESSION_CONTROL::AttachedThreadStart, (VOID*)1);
    PIN_AddThreadDetachProbedFunction(DETACH_SESSION::DetachThreadStart, (VOID*)1);

    THREADID tid = PIN_SpawnInternalThread(SESSION_CONTROL::DedicatedThread, NULL, 0, NULL);
    assert(tid != -1);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
