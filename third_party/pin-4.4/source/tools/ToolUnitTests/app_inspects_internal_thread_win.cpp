/*
 * Copyright (C) 2014-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * A tool that creates internal Pin thread
 * and verifies that the thread is finished gracefully.
 */

#include "pin.H"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <windows/pinrt_windows.h>





/*!
 * Global variables.
 */

// UID of the internal thread. It is created in the application thread by the
// main() tool's procedure.
PIN_THREAD_UID intThreadUid;
// Pointer to TID of internal thread. Imported from application.
unsigned int* pTid;

//==========================================================================
// Utilities
//==========================================================================

/*!
 * Print out the error message and exit the process.
 */
static void AbortProcess(const std::string& msg, unsigned long code)
{
    std::cerr << "Test aborted: " << msg << " with code " << code << std::endl;
    PIN_WriteErrorMessage(msg.c_str(), 1002, PIN_ERR_FATAL, 0);
}

/*!
 * Internal tool's thread. It is created in the application thread by the
 * main() tool's procedure.
 */
static VOID IntThread(VOID* arg)
{
    WINDOWS::HMODULE exeHandle = WINDOWS::GetModuleHandle(NULL);
    pTid                       = (unsigned int*)WINDOWS::GetProcAddress(exeHandle, "tid");

    // Sets current TID in imported variable, makes it available to application.
    *pTid = WINDOWS::GetCurrentThreadId();

    // tid value is reset in Fini.
    while (*pTid != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Finishes gracefully if application doesn't harm the thread.
    PIN_ExitThread(0);
}

//==========================================================================
// Instrumentation callbacks
//==========================================================================
/*!
 * Process exit callback (unlocked).
 */
static VOID PrepareForFini(VOID* v)
{
    BOOL waitStatus;
    INT32 threadExitCode;

    // Notify internal thread to finish.
    *pTid = 0;

    // First, wait for termination of the main internal thread. When this thread exits,
    // all secondary internal threads are already created and, so <uidSet> can be safely
    // accessed without lock.
    waitStatus = PIN_WaitForThreadTermination(intThreadUid, 1000, &threadExitCode);
    if (!waitStatus)
    {
        AbortProcess("PIN_WaitForThreadTermination(RootThread) failed", 0);
    }
    if (threadExitCode != 0)
    {
        AbortProcess("Tool's thread exited abnormally", threadExitCode);
    }

    std::cerr << "Tool's thread finished successfully." << std::endl;
}

/*!
 * Process exit callback.
 */
static VOID Fini(INT32 code, VOID* v)
{
    if (code != 0)
    {
        AbortProcess("Application exited abnormally", code);
    }
}

/*!
 * The main procedure of the tool.
 */
int main(int argc, char* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    PIN_AddPrepareForFiniFunction(PrepareForFini, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Spawn the main internal thread. When this thread starts it spawns all other internal threads.
    THREADID intThreadId = PIN_SpawnInternalThread(IntThread, NULL, 0, &intThreadUid);
    if (intThreadId == INVALID_THREADID)
    {
        AbortProcess("PIN_SpawnInternalThread(intThread) failed", 0);
    }

    // Never returns
    PIN_StartProgram();
    return 0;
}
