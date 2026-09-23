/*
 * Copyright (C) 2016-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <atomic>
#include <iostream>

#include <sched.h>

/* ===================================================================== */
/* Global variables and declarations */
/* ===================================================================== */

static std::atomic_int isAppStarted {0};

static std::atomic_int isReadyForDetach {0};

static std::atomic_int isReadyForAttach {0};

static std::atomic_int pinCompletelyAttached {0};

/* ===================================================================== */

int rep_PinIsAttached() { return pinCompletelyAttached.load(std::memory_order_seq_cst); }

int rep_PinIsDetached() { return 0; }

void rep_FirstProbeInvoked()
{
    std::cerr << "rep_FirstProbeInvoked" << std::endl;
    isReadyForDetach.store(1, std::memory_order_seq_cst);
}

void rep_SecondProbeInvoked()
{
    std::cerr << "rep_SecondProbeInvoked" << std::endl;
    isReadyForDetach.store(1, std::memory_order_seq_cst);
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (isReadyForAttach == 1)
    {
        //can't get callbacks from pin after detach completion
        std::cerr << "failure - got follow child notification when pin is detached" << std::endl;
        exit(-1);
    }

    if (!IMG_IsMainExecutable(img))
    {
        return;
    }

    std::cout << "ImageLoad called for " << IMG_Name(img) << std::endl << std::flush;

    RTN rtn = RTN_FindByName(img, "PinIsAttached");
    ASSERTX(RTN_Valid(rtn));
    ASSERTX(0 != RTN_ReplaceProbed(rtn, AFUNPTR(rep_PinIsAttached)));

    rtn = RTN_FindByName(img, "PinIsDetached");
    ASSERTX(RTN_Valid(rtn));
    ASSERTX(0 != RTN_ReplaceProbed(rtn, AFUNPTR(rep_PinIsDetached)));

    rtn = RTN_FindByName(img, "FirstProbeInvoked");
    ASSERTX(RTN_Valid(rtn));
    ASSERTX(0 != RTN_ReplaceProbed(rtn, AFUNPTR(rep_FirstProbeInvoked)));

    rtn = RTN_FindByName(img, "SecondProbeInvoked");
    ASSERTX(RTN_Valid(rtn));
    ASSERTX(0 != RTN_ReplaceProbed(rtn, AFUNPTR(rep_SecondProbeInvoked)));

    pinCompletelyAttached.store(1, std::memory_order_seq_cst);
}

VOID DetachComplete(VOID* v)
{
    std::cout << "DetachComplete called" << std::endl << std::flush;
    isReadyForDetach      = 0;
    isAppStarted          = 0;
    pinCompletelyAttached = 0;
    isReadyForAttach.store(1, std::memory_order_seq_cst);
}

VOID AppStart(VOID* v) { isAppStarted.store(1, std::memory_order_seq_cst); }

VOID AttachMain(VOID* v)
{
    std::cout << "AttachMain called" << std::endl << std::flush;
    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_AddApplicationStartFunction(AppStart, 0);

    PIN_AddDetachFunctionProbed(DetachComplete, 0);

    isReadyForAttach.store(0, std::memory_order_seq_cst);
}

VOID ThreadProc(VOID* p)
{
    while (isReadyForDetach.load(std::memory_order_seq_cst) == 0)
    {
        sched_yield();
    }
    PIN_DetachProbed();

    while (isReadyForAttach.load(std::memory_order_seq_cst) == 0)
    {
        sched_yield();
    }

    PIN_AttachProbed(AttachMain, 0);

    while (isReadyForDetach.load(std::memory_order_seq_cst) == 0)
    {
        sched_yield();
    }
    PIN_DetachProbed();

    while (isReadyForAttach.load(std::memory_order_seq_cst) == 0)
    {
        sched_yield();
    }

    PIN_AttachProbed(AttachMain, 0);
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_AddApplicationStartFunction(AppStart, 0);

    PIN_AddDetachFunctionProbed(DetachComplete, 0);

    // Create a detached internal thread to handle attach/detach requests
    (VOID) PIN_SpawnInternalThread(ThreadProc, nullptr, 0, nullptr);

    // Never returns
    PIN_StartProgramProbed();

    return 0;
}
