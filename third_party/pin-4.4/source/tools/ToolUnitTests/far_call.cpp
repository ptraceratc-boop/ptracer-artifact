/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
  * This pintool validates a FAR CALL indirect via memory.
  * When the FAR CALL memory operand is valid, no exceptions are expected.
  * The memory operand can be corrupt in two ways:
  * 1.  The lower 2 bits (RPL) of the segment selector word is zero, in which case 
  *     Pin emulation snhould raise #GP. This exception is intercepted by the pintool.
  * 2.  The RPL is valid but the rest of the segment selector is zero (trying to call to
  *     a different segment). In this case Pin emulation will assert.
  */
#include "pin.H"
#include <iostream>

STATIC KNOB< BOOL > KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "verbose", "0", "verbose mode");
STATIC KNOB< BOOL > KnobExceptionSigsegv(KNOB_MODE_WRITEONCE, "pintool", "sigsegv", "0", "Expect segmentation fault");

static BOOL gotException = false;

static void OnException(THREADID threadIndex, CONTEXT_CHANGE_REASON reason, const CONTEXT* ctxtFrom, CONTEXT* ctxtTo, INT32 info,
                        VOID* v)
{
    if (KnobVerbose)
    {
        std::cout << "Received Exception, reason = " << std::dec << reason << " info = " << info << std::endl;
    }
    gotException = TRUE;
    ASSERTX(CONTEXT_CHANGE_REASON_FATALSIGNAL == reason);
    ASSERTX(SIGSEGV == info);
}

VOID Fini(INT32 code, VOID* v)
{
    ASSERT(KnobExceptionSigsegv.Value() == gotException,
           (gotException ? "Got unexpected exception" : "Should have gotten exception"));
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        std::cerr << "ERROR: PIN_Init failed" << std::endl;
        return 1;
    }

    PIN_AddContextChangeFunction(OnException, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
    return 0;
}
