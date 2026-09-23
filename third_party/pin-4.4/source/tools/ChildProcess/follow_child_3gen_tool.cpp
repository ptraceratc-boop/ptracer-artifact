/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
This tool is used to output that the child process was created in the correct mode 
(probe or jit)
*/

#include "pin.H"
#include <iostream>

#include <windows/pinrt_windows.h>




WINDOWS::HANDLE outputMutex;

// use mutex to synchronize outputs from multiple instances of this tool
void MutexWriteToStdout(char* msg)
{
    WINDOWS::WaitForSingleObject(outputMutex, INFINITE);

    printf(msg);

    fflush(stdout);

    WINDOWS::ReleaseMutex(outputMutex);
}

int main(INT32 argc, CHAR** argv)
{
    outputMutex = WINDOWS::CreateMutex(NULL, false /* not initial owner*/, "pin_child_tool_output_mutex");
    if (outputMutex == NULL)
    {
        std::cout << "failed to create outputMutex\n";
        exit(0);
    }

    PIN_InitSymbols();
    PIN_Init(argc, argv);

    // Never returns
    if (PIN_IsProbeMode())
    {
        MutexWriteToStdout("In follow_child PinTool is probed 1\n");
        PIN_StartProgramProbed();
    }
    else
    {
        MutexWriteToStdout("In follow_child PinTool is probed 0\n");
        PIN_StartProgram();
    }

    return 0;
}
