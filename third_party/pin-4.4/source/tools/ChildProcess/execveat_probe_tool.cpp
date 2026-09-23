/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <stdio.h>
#include <unistd.h>

/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    fprintf(stdout, "pid before exec child: %u\n", getpid());
    return TRUE;
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID* v) { fprintf(stdout, "Probe Fini called\n"); }

/* ===================================================================== */

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    LOG("Starting execveat_probe_tool\n");

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgramProbed();

    return 0;
}
