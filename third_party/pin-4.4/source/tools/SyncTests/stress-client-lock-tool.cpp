/*
 * Copyright (C) 2014-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"

#include <chrono>
#include <iostream>
#include <thread>

typedef void (*AppFunSig)(unsigned int, pid_t);
AppFunSig OrigThreadInit = NULL;
AppFunSig OrigThreadFini = NULL;

static RTN GetProbableRtn(IMG img, const char* name)
{
    RTN rtn = RTN_FindByName(img, name);
    if (!RTN_Valid(rtn))
    {
        std::cerr << "TOOL ERROR: Unable to find the function \"" << name << "\"" << std::endl;
        PIN_ExitProcess(101);
    }
    return rtn;
}

static RTN GetReplacableRtn(IMG img, const char* name)
{
    RTN rtn = RTN_FindByName(img, name);
    if (!RTN_Valid(rtn))
    {
        std::cerr << "TOOL ERROR: Unable to find the function \"" << name << "\"" << std::endl;
        PIN_ExitProcess(103);
    }
    if (!RTN_IsSafeForProbedReplacement(rtn))
    {
        std::cerr << "TOOL ERROR: The function \"" << name << "\" is not safe for replacement" << std::endl;
        PIN_ExitProcess(104);
    }
    return rtn;
}

static void OnSecondaryThreadInit(unsigned int threadNum, pid_t tid)
{
    PIN_LockClient();
    if (NULL == OrigThreadInit)
    {
        std::cerr << "TOOL ERROR: Attempting to call SecondaryThreadInit but it is a NULL pointer" << std::endl;
        PIN_ExitProcess(105);
    }
    OrigThreadInit(threadNum, tid);
    PIN_UnlockClient();
}

static void OnSecondaryThreadFini(unsigned int threadNum, pid_t tid)
{
    PIN_LockClient();
    if (NULL == OrigThreadFini)
    {
        std::cerr << "TOOL ERROR: Attempting to call SecondaryThreadFini but it is a NULL pointer" << std::endl;
        PIN_ExitProcess(106);
    }
    OrigThreadFini(threadNum, tid);
    PIN_UnlockClient();
}

static void OnSecondaryThreadWork()
{
    PIN_LockClient();
    std::cout << "TOOL: (" << PIN_GetTid() << ") executing OnSecondaryThreadWork" << std::endl << std::flush;
    std::this_thread::yield();
    PIN_UnlockClient();
}

static void OnReleaseThreads(ADDRINT doRelease)
{
    PIN_LockClient();
    *((bool*)doRelease) = true;
    std::cout << "TOOL: Released the threads, now waiting a few seconds for them to reach the lock." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    PIN_UnlockClient();
}

static VOID Image(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img)) return;

    RTN secondaryThreadInitRtn = GetReplacableRtn(img, "SecondaryThreadInit");
    OrigThreadInit             = (AppFunSig)RTN_ReplaceProbed(secondaryThreadInitRtn, AFUNPTR(OnSecondaryThreadInit));

    RTN secondaryThreadFiniRtn = GetReplacableRtn(img, "SecondaryThreadFini");
    OrigThreadFini             = (AppFunSig)RTN_ReplaceProbed(secondaryThreadFiniRtn, AFUNPTR(OnSecondaryThreadFini));

    RTN secondaryThreadWorkRtn = GetProbableRtn(img, "SecondaryThreadWork");
    if (RTN_InsertCallProbed(secondaryThreadWorkRtn, IPOINT_BEFORE, AFUNPTR(OnSecondaryThreadWork), IARG_END))
    {
        ASSERTX(RTN_IsSafeForProbedInsertion(secondaryThreadWorkRtn));
    }
    else
    {
        std::cerr << "TOOL ERROR: The function \"SecondaryThreadWork\" is not safe for probe insertion" << std::endl;
        PIN_ExitProcess(102);
    }

    RTN releaseThreadsRtn = GetProbableRtn(img, "ReleaseThreads");
    if (RTN_InsertCallProbed(releaseThreadsRtn, IPOINT_BEFORE, AFUNPTR(OnReleaseThreads), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_END))
    {
        ASSERTX(RTN_IsSafeForProbedInsertion(releaseThreadsRtn));
    }
    else
    {
        std::cerr << "TOOL ERROR: The function \"ReleaseThreads\" is not safe for probe insertion" << std::endl;
        PIN_ExitProcess(102);
    }
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);
    PIN_InitSymbols();

    IMG_AddInstrumentFunction(Image, 0);

    PIN_StartProgram(); // never returns
    return 0;
}
