/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <fstream>
#include <iostream>

#if defined(TARGET_IA32E)
constexpr int SIGNUM_MAX = 64; // Number of signals
#else
constexpr int SIGNUM_MAX = 32; // Number of signals
#endif

static bool checked      = false;

/*
* Note: sigismember_unfiltered is doing exactly what sigismember does. There is no filtering in this function, so we assume it is working as expected. 
*/

VOID check_sig_APIs()
{
    sigset_t set;

    // Test sigemptyset_unfiltered
    ASSERTX(0 == sigemptyset_unfiltered(&set));
    for (int signum = 1; signum <= SIGNUM_MAX; ++signum)
    {
        ASSERTX(!sigismember_unfiltered(&set, signum));
    }

    // Test sigfillset_unfiltered
    ASSERTX(0 == sigfillset_unfiltered(&set));
    for (int signum = 1; signum <= SIGNUM_MAX; ++signum)
    {
        ASSERTX(sigismember_unfiltered(&set, signum));
    }
    // Test sigaddset_unfiltered, sigdelset_unfiltered and sigismember_unfiltered for signals 1 to 64
    for (int signum = 1; signum <= SIGNUM_MAX; ++signum)
    {
        ASSERTX(0 == sigaddset_unfiltered(&set, signum));
        ASSERTX(sigismember_unfiltered(&set, signum));
        ASSERTX(0 == sigdelset_unfiltered(&set, signum));
        ASSERTX(!sigismember_unfiltered(&set, signum));
    }
}

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool tests the unfiltered signal APIs " << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

// Instrumentation function
VOID Image(IMG img, VOID* v)
{
    //Check Only once.
    if (!checked)
    {
        checked = true;
        check_sig_APIs();
    }
}

// Main function for the Pin tool
int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }
    IMG_AddInstrumentFunction(Image, 0);
    PIN_StartProgramProbed();
    return 0;
}