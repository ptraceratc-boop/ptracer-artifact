/*
 * Copyright (C) 2009-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <cstring>
#include <iostream>
#include <stdio.h>
#include <vector>

/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    INT pinArgc;
    CHAR const* const* pinArgv;
    CHILD_PROCESS_GetPinCommandLine(cProcess, &pinArgc, &pinArgv);

    std::vector< const CHAR* > probePinArgv;
    BOOL probeAdded = FALSE;
    for (INT argumentIndex = 0; argumentIndex < pinArgc; ++argumentIndex)
    {
        if (!probeAdded && std::strcmp(pinArgv[argumentIndex], "-t") == 0)
        {
            // Add -probe before -t to ensure that the child process is launched in probe mode.
            probePinArgv.push_back("-probe");
            probeAdded = TRUE;
        }
        probePinArgv.push_back(pinArgv[argumentIndex]);
    }
    ASSERTX(probeAdded);
    CHILD_PROCESS_SetPinCommandLine(cProcess, static_cast< INT >(probePinArgv.size()), probePinArgv.data());

    return TRUE;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    BOOL jitMode = (v == 0);
    if (IMG_IsMainExecutable(img))
    {
        fprintf(stdout, "Image %s is loaded in %s mode\n", IMG_Name(img).c_str(), (jitMode ? "JIT" : "PROBE"));
        // Docs say that PIN_GetWindowsSyscallFromKey is only supported in JIT mode,
        // but it is safe to call it in PROBE mode as well, it will just return
        // SYSCALL_NUMBER_INVALID for all keys.
        // We use this to verify that syscall discovery is disabled in PROBE mode.
        auto ntReadFileSysId = PIN_GetWindowsSyscallFromKey(SYSCALL_KEY_NtReadFile);
        if (jitMode)
        {
            ASSERTX(SYSCALL_NUMBER_INVALID != ntReadFileSysId);
        }
        else
        {
            ASSERTX(SYSCALL_NUMBER_INVALID == ntReadFileSysId);
        }
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    // Never returns
    if (PIN_IsProbeMode())
    {
        IMG_AddInstrumentFunction(ImageLoad, (VOID*)1);
    }
    else
    {
        IMG_AddInstrumentFunction(ImageLoad, 0);
    }
    PIN_StartProgram();
    return 0;
}
