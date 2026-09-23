/*
 * Copyright (C) 2008-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <vector>

#include <windows/pinrt_windows.h>





/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

// General configuration

// Pin full path
KNOB< std::string > KnobPinFullPath(KNOB_MODE_WRITEONCE, "pintool", "pin_path", "", "pin full path");
// Tool full path
KNOB< std::string > KnobToolsFullPath(KNOB_MODE_WRITEONCE, "pintool", "tools_path", "", "grand parent tool full path");

// Parent configuration

// Application name
KNOB< std::string > KnobParentApplicationName(KNOB_MODE_WRITEONCE, "pintool", "parent_app_name", "win_parent_process",
                                              "parent application name");
// PinTool name
KNOB< std::string > KnobParentToolName(KNOB_MODE_WRITEONCE, "pintool", "parent_tool_name", "parent_tool",
                                       "parent tool full path");
// Current process id received by grand_parent tool
KNOB< OS_PROCESS_ID > KnobCurrentProcessId(KNOB_MODE_WRITEONCE, "pintool", "process_id", "0", "current process id");
// Whether to check current process id received in KnobCurrentProcessId
KNOB< BOOL > KnobCheckCurrentProcessId(KNOB_MODE_WRITEONCE, "pintool", "check_process_id", "0", "current process id");

// Child configuration

// Application name
KNOB< std::string > KnobChildApplicationName(KNOB_MODE_WRITEONCE, "pintool", "child_app_name", "win_child_process",
                                             "child application name");
// PinTool name
KNOB< std::string > KnobChildToolName(KNOB_MODE_WRITEONCE, "pintool", "child_tool_name", "follow_child_3gen_tool",
                                      "child tool full path");
// Whether to probe the child
KNOB< BOOL > KnobProbeChild(KNOB_MODE_WRITEONCE, "pintool", "probe_child", "0", "probe the child process");

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    BOOL res;
    INT appArgc;
    CHAR const* const* appArgv;
    OS_PROCESS_ID pid = CHILD_PROCESS_GetId(cProcess);

    CHILD_PROCESS_GetCommandLine(cProcess, &appArgc, &appArgv);

    //Inject only if the KnobChildApplicationName value is current child process application
    std::string childAppToInject = KnobChildApplicationName.Value();
    std::string childApp(appArgv[0]);
    std::string::size_type index = childApp.find(childAppToInject);
    if (index == std::string::npos)
    {
        return FALSE;
    }

    //Set Pin's command line for child process
    INT pinArgc          = 0;
    const INT pinArgcMax = 6;
    std::vector<const char*> pinArgv;

    std::string pin         = KnobPinFullPath.Value() + "pin";
    pinArgv.push_back(pin.c_str());
    pinArgv.push_back("-follow_execv");
    if (KnobProbeChild)
    {
        pinArgv.push_back("-probe"); // pin in probe mode
    }
    pinArgv.push_back("-t");
    std::string tool = KnobToolsFullPath.Value() + "/" + KnobChildToolName.Value();
    tool += ".dll";
    pinArgv.push_back(tool.c_str());
    pinArgv.push_back("--");
    pinArgc = pinArgv.size();

    ASSERTX(pinArgc <= pinArgcMax);

    CHILD_PROCESS_SetPinCommandLine(cProcess, pinArgc, pinArgv.data());

    return TRUE;
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    std::cout << "In parent_tool PinTool is probed " << decstr(PIN_IsProbeMode()) << std::endl;

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    //Verify KnobCurrentProcessId is accurate
    if (KnobCheckCurrentProcessId == TRUE)
    {
        if (WINDOWS::GetCurrentProcessId() != KnobCurrentProcessId)
        {
            std::cout << "Got wrong process id in KnobCurrentProcessId" << std::endl;
            exit(0);
        }
    }

    // Never returns
    if (PIN_IsProbeMode())
    {
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}
