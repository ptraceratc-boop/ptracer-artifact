/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This tool runs an application until CTRL-C (SIGINT) is received,
 * then terminates the application gracefully.
 * 
 * Usage:
 *   pin -t ctrl_c_terminator.so -- <application>
 */

#include "pin.H"
#include <iostream>
#include <signal.h>

using std::cout;
using std::endl;
using std::string;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< BOOL > KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "v", "1", "Verbose output");

/* ===================================================================== */
/* Signal Handler */
/* ===================================================================== */

/*!
 * Signal handler for SIGINT (CTRL-C)
 * @param[in] sig       Signal number
 * @param[in] ctxt      Context when signal was received
 * @param[in] appSig    True if application sent the signal
 * @param[in] v         Callback value
 */
static BOOL SignalHandler(THREADID tid, INT32 sig, CONTEXT* ctxt, BOOL hasHandler, const EXCEPTION_INFO* pExceptInfo, VOID* v)
{
    if (sig == SIGINT)
    {
        cout << "\n[PIN Tool] Received SIGINT (CTRL-C), terminating application..." << endl;

        // Exit the application immediately
        PIN_ExitApplication(0);

        // Return FALSE to prevent signal from being delivered to application
        return FALSE;
    }

    // Let other signals pass through
    return TRUE;
}

/* ===================================================================== */
/* Finalization */
/* ===================================================================== */

/*!
 * This function is called when the application exits
 */
VOID Fini(INT32 code, VOID* v)
{
    if (KnobVerbose)
    {
        cout << "[PIN Tool] Application terminated with code: " << code << endl;
    }
}

/* ===================================================================== */
/* Print Help Message */
/* ===================================================================== */

INT32 Usage()
{
    cout << "This tool runs an application until CTRL-C is pressed, then terminates it." << endl;
    cout << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize Pin
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    if (KnobVerbose)
    {
        cout << "[PIN Tool] CTRL-C Terminator loaded" << endl;
        cout << "[PIN Tool] Press CTRL-C to terminate the application" << endl;
    }

    // Register signal handler for SIGINT
    PIN_InterceptSignal(SIGINT, SignalHandler, 0);

    // Unblock SIGINT to ensure we can receive it
    PIN_UnblockSignal(SIGINT, TRUE);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program - never returns
    PIN_StartProgram();

    return 0;
}
