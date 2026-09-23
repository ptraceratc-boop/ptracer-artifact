/*
 * Copyright (C) 2009-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>

#include <windows/pinrt_windows.h>





// Whether to check current process id received in KnobCurrentProcessId
KNOB< BOOL > KnobLoadSystemDlls(KNOB_MODE_WRITEONCE, "pintool", "load_system_dlls", "0", "load system dlls in main()");

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    std::cout << "Tool: At follow child callback" << std::endl << std::flush;
    std::cout << "Tool: Child process id = " << CHILD_PROCESS_GetId(cProcess) << std::endl << std::flush;
    return TRUE;
}

VOID ImageLoad(IMG img, VOID* v) { std::cout << "Tool: Loading " << IMG_Name(img) << ", Image id = " << IMG_Id(img) << std::endl << std::flush; }

// Pin calls this function every time a new img is unloaded
// You can't instrument an image that is about to be unloaded
VOID ImageUnload(IMG img, VOID* v) { std::cout << "Tool: Unloading " << IMG_Name(img) << ", Image id = " << IMG_Id(img) << std::endl << std::flush; }

VOID AppStart(VOID* v) { std::cout << "Tool: Application started" << std::endl << std::flush; }

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    std::cout << "Tool: In tool's main, probed = " << decstr(PIN_IsProbeMode()) << std::endl << std::flush;
    std::cout << "Tool: Current process id = " << WINDOWS::GetCurrentProcessId() << std::endl << std::flush;

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    IMG_AddUnloadFunction(ImageUnload, 0);

    if (KnobLoadSystemDlls)
    {
        WINDOWS::HMODULE h1 = WINDOWS::LoadLibrary("RPCRT4.dll");
        if (h1 == NULL)
        {
            std::cout << "Failed to load RPCRT4" << std::endl << std::flush;
            exit(-1);
        }
        WINDOWS::HMODULE h2 = WINDOWS::LoadLibrary("advapi32.dll");
        if (h1 == NULL)
        {
            std::cout << "Failed to load advapi32" << std::endl << std::flush;
            exit(-1);
        }
        WINDOWS::HMODULE h3 = WINDOWS::LoadLibrary("dbghelp.dll");
        if (h1 == NULL)
        {
            std::cout << "Failed to load dbghelp" << std::endl << std::flush;
            exit(-1);
        }
        WINDOWS::HMODULE h4 = WINDOWS::LoadLibrary("user32.dll");
        if (h1 == NULL)
        {
            std::cout << "Failed to load user32" << std::endl << std::flush;
            exit(-1);
        }
    }

    // Never returns
    if (PIN_IsProbeMode())
    {
        PIN_AddApplicationStartFunction(AppStart, 0);
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}
