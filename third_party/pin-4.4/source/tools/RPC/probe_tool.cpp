/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 * A probe tool that does nothing specific, but we need it in probe mode to enable Pin client facilities then end up
 * placing probes on fork() function to that the SYS_notify_fork_in_parent and SYS_notify_fork_in_child system calls 
 * will be called after fork()
 */

/* ===================================================================== */
#include "pin.H"

/* ===================================================================== */
/* Analysis routines  */
/* ===================================================================== */

/* ===================================================================== */
/* Instrumentation routines  */
/* ===================================================================== */

/* ===================================================================== */

int main(INT32 argc, CHAR* argv[])
{
    PIN_Init(argc, argv);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
