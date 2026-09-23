/*
 * Copyright (C) 2006-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  Test detaching Pin from running process in probe mode.
 *  The application must have a TellPinToDetach function for the tool to replace in order to perform detach.
 *
 *  detach_probed.test:
 *   This test places several instrumentations to verify that they are called in the
 *   correct order and that all the probes are properly removed after detach.
 *   This tool assumes that the application is detach_probed_app.exe
 *   It also tests PROBE on PROBE for both replace and insert.
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "tool_macros.h"

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

unsigned long* updateWhenReadyPtr = 0;

KNOB< BOOL > KnobCallReplaceSignatureProbed(KNOB_MODE_WRITEONCE, "pintool", "replace_signature_probed", "0",
                                            "Use ReplaceSignatureProbed() API instead of ReplaceProbed()");

KNOB< BOOL > KnobBlockingReadDuringDetach(KNOB_MODE_WRITEONCE, "pintool", "blocking_read_during_detach", "0",
                                          "Do blocking read during detch from analysis rooutine");

typedef void (*t_app_function)(int);

t_app_function SomeFuntionToProbe_1 = NULL;
t_app_function SomeFuntionToProbe_2 = NULL;

// Replacement function for RTN_ReplaceSignatureProbed() without alignment constrains
VOID DetachPinFromMTApplication_WithoutAlignment(unsigned long* updateWhenReady)
{
    updateWhenReadyPtr = updateWhenReady;
    fprintf(stderr, "Pin tool: sending detach request\n");
    PIN_DetachProbed();
}

// Replacement function for RTN_ReplaceProbed() with alignment constrains in Linux 32-bit
#if defined(TARGET_LINUX) && defined(TARGET_IA32)
// Request the compiler to align the stack to 16 bytes boundary, since this
// function might called with application stack which might not be aligned properly
__attribute__((force_align_arg_pointer))
#endif
VOID DetachPinFromMTApplication_WithAlignment(unsigned long *updateWhenReady)
{
    updateWhenReadyPtr = updateWhenReady;
    fprintf(stderr, "Pin tool: sending detach request\n");
    PIN_DetachProbed();
}

void Before_SomeFuntionToProbe_1(int phase)
{
    std::cout << "Pintool [1]: BEFORE SomeFuntionToProbe(). Phase " << phase << std::endl << std::flush;
    ASSERTX(2 != phase); // This is just to show that the replacement function is called
}

void Before_SomeFuntionToProbe_2(int phase)
{
    std::cout << "Pintool [2]: BEFORE SomeFuntionToProbe(). Phase " << phase << std::endl << std::flush;
    ASSERTX(2 != phase); // This is just to show that the replacement function is called
}

void After_SomeFuntionToProbe_1() { std::cout << "Pintool [1]: AFTER SomeFuntionToProbe()." << std::endl << std::flush; }

void After_SomeFuntionToProbe_2() { std::cout << "Pintool [2]: AFTER SomeFuntionToProbe()." << std::endl << std::flush; }

void Replace_SomeFuntionToProbe_1(int phase)
{
    std::cout << "Pintool [1]: REPLACE SomeFuntionToProbe(). Phase " << phase << std::endl << std::flush;
    ASSERTX(2 != phase); // Verify this function is not called after detach
    ASSERTX(NULL != SomeFuntionToProbe_1);
    SomeFuntionToProbe_1(phase);
}

void Replace_SomeFuntionToProbe_2(int phase)
{
    std::cout << "Pintool [2]: REPLACE SomeFuntionToProbe(). Phase " << phase << std::endl << std::flush;
    ASSERTX(2 != phase); // Verify this function is not called after detach
    ASSERTX(NULL != SomeFuntionToProbe_2);
    SomeFuntionToProbe_2(phase);
}

int pipefd[2];

/**
 * This function is called before some function being executed in the application on some secondary thread.
 * It will be blocking until PIN_DetachProbed() and will be unblocked (by closing the output side of the pipe) 
 * at the end of the detach. The purpose of this it to test that the detach flow doesn't get stuck due to a blocking system call
 * happening in one of the application thread (PINT-6662)
 * 
 */
void Before_blocking_function()
{
    char buffer[256];
    ssize_t result;

    // Can't print here from secondary thread since makefile does diff with reference (multiple threads)
    // Enable if needed for running manually
    // printf("Thread: Starting blocking read...\n");

    // This will BLOCK waiting for user input
    result = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);

    if (result == 0)
    {
        // Should get only here
        std::cout << "Thread: EOF reached - write end closed, exiting cleanly" << std::endl << std::flush;
        return;
    }
    else if (result > 0)
    {
        std::cout << "Thread: Read " << result << " bytes" << std::endl << std::flush;
    }
    else
    {
        std::cout << "Thread: read failed" << std::endl << std::flush;
        perror("Thread: read failed");
    }
    // Shouldn't get here.
    ASSERTX(0);
}

VOID DetachCompleted(VOID* v)
{
    fprintf(stderr, "Pin tool: detach is completed\n");
    *updateWhenReadyPtr = 1;

    if (KnobBlockingReadDuringDetach)
    {
        // Close the fd - this will unblock the read() with EBADF
        // This occurs after most of the detach finished.
        close(pipefd[1]);
    }
}

VOID ImageLoad(IMG img, void* v)
{
    if (!IMG_IsMainExecutable(img))
    {
        return;
    }

    RTN rtn = RTN_FindByName(img, C_MANGLE("TellPinToDetach"));
    if (RTN_Valid(rtn))
    {
        if (KnobCallReplaceSignatureProbed)
        {
            PROTO proto_func =
                PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "TellPinToDetach", PIN_PARG(unsigned long*), PIN_PARG_END());

            RTN_ReplaceSignatureProbed(rtn, AFUNPTR(DetachPinFromMTApplication_WithoutAlignment), IARG_PROTOTYPE, proto_func,
                                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        }
        else
        {
            RTN_ReplaceProbed(rtn, AFUNPTR(DetachPinFromMTApplication_WithAlignment));
        }
    }

    rtn = RTN_FindByName(img, C_MANGLE("SomeFuntionToProbe"));
    if (RTN_Valid(rtn))
    {
        // We are checking that PROBE on PROBE works both for replace before insert, and insert before replace.
        // We also verify using a reference file that replaced functions are called after INSERT_BEFORE calls
        // and that the order of inserted function is the instrumentation order while the order of replace function
        // is the inverse order.
        PROTO proto_func =
            PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "SomeFuntionToProbe", PIN_PARG(int), PIN_PARG_END());
        SomeFuntionToProbe_1 = (t_app_function)RTN_ReplaceProbed(rtn, AFUNPTR(Replace_SomeFuntionToProbe_1));
        ASSERTX(RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_SomeFuntionToProbe_1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                                     IARG_END));
        ASSERTX(
            RTN_InsertCallProbed(rtn, IPOINT_AFTER, AFUNPTR(After_SomeFuntionToProbe_1), IARG_PROTOTYPE, proto_func, IARG_END));
        SomeFuntionToProbe_2 = (t_app_function)RTN_ReplaceProbed(rtn, AFUNPTR(Replace_SomeFuntionToProbe_2));
        ASSERTX(RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_SomeFuntionToProbe_2), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                                     IARG_END));
        ASSERTX(
            RTN_InsertCallProbed(rtn, IPOINT_AFTER, AFUNPTR(After_SomeFuntionToProbe_2), IARG_PROTOTYPE, proto_func, IARG_END));
    }

    if (KnobBlockingReadDuringDetach)
    {
        rtn = RTN_FindByName(img, C_MANGLE("SomeblockingFuntionToProbe"));
        if (RTN_Valid(rtn))
        {
            PROTO proto_func = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "SomeblockingFuntionToProbe", PIN_PARG_END());
            ASSERTX(RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(Before_blocking_function), IARG_PROTOTYPE, proto_func,
                                         IARG_END));
        }
    }
}
/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    if (KnobBlockingReadDuringDetach)
    {
        pipe(pipefd);
    }

    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddDetachFunctionProbed(DetachCompleted, 0);
    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
