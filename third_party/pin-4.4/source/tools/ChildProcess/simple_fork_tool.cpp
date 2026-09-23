/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>

/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

KNOB< BOOL > KnobToolProbeMode(KNOB_MODE_WRITEONCE, "pintool", "probe", "0", "invoke tool in probe mode");
KNOB< BOOL > KnobToolSanity(KNOB_MODE_WRITEONCE, "pintool", "sanity", "0", "Use PIN_AddForkFunction to check f");

/* ===================================================================== */

VOID AfterForkInChildProbe(UINT32 childPid, void* data) { LOG("AfterForkInChild:  hello from " + decstr(getpid()) + "\n"); }

VOID AfterForkInChild(THREADID threadid, const CONTEXT* ctxt, VOID* arg)
{
    LOG("AfterForkInChild:  hello from " + decstr(getpid()) + "\n");
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    LOG("Hello from tool\n");

    // Never returns
    if (KnobToolProbeMode)
    {
        if (KnobToolSanity)
        {
            PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_CHILD, AfterForkInChildProbe, (VOID*)0);
        }
        PIN_StartProgramProbed();
    }
    else
    {
        if (KnobToolSanity)
        {
            PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChild, (VOID*)0);
        }
        PIN_StartProgram();
    }
    return 0;
}
