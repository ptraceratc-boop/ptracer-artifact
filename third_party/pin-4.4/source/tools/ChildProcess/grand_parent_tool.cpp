/*
 * Copyright (C) 2008-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <vector>
#include <unistd.h>






/* ===================================================================== */
/* Command line Switches */
/* ===================================================================== */

//General configuration - Pin and Tool full path
KNOB< std::string > KnobPinFullPath(KNOB_MODE_WRITEONCE, "pintool", "pin_path", "", "pin full path");

KNOB< std::string > KnobToolsFullPath(KNOB_MODE_WRITEONCE, "pintool", "tools_path", "", "grand parent tool full path");

//Parent configuration - Application and PinTool name
KNOB< std::string > KnobParentApplicationName(KNOB_MODE_WRITEONCE, "pintool", "parent_app_name", "win_parent_process",
                                              "parent application name");

KNOB< std::string > KnobParentToolName(KNOB_MODE_WRITEONCE, "pintool", "parent_tool_name", "parent_tool",
                                       "parent tool full path");

//Child configuration - Application and PinTool name
KNOB< std::string > KnobChildApplicationName(KNOB_MODE_WRITEONCE, "pintool", "child_app_name", "win_child_process",
                                             "child application name");

KNOB< std::string > KnobChildToolName(KNOB_MODE_WRITEONCE, "pintool", "child_tool_name", "follow_child_3gen_tool",
                                      "child tool full path");

// Whether to probe the child
KNOB< BOOL > KnobProbeChild(KNOB_MODE_WRITEONCE, "pintool", "probe_child", "0", "probe the child process");

// Whether to probe the grand child
KNOB< BOOL > KnobProbeGrandChild(KNOB_MODE_WRITEONCE, "pintool", "probe_grand_child", "0", "probe the grand child process");

// current directory
std::string currDirStr;

/* ===================================================================== */
//Helper function
std::string Int64ToDecimalString(INT64 number)
{
    const UINT32 BUFFER_SIZE = 64;
    CHAR buffer[BUFFER_SIZE] = {0};

    buffer[BUFFER_SIZE - 1] = '\0';
    INT32 index;
    for (index = BUFFER_SIZE - 2; (number != 0) && (index >= 0); index--, number /= 10)
    {
        buffer[index] = '0' + number % 10;
    }
    return std::string(&buffer[index + 1]);
}

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    BOOL res;
    INT appArgc;
    CHAR const* const* appArgv;
    OS_PROCESS_ID pid = CHILD_PROCESS_GetId(cProcess);

    CHILD_PROCESS_GetCommandLine(cProcess, &appArgc, &appArgv);

    //Inject only if the KnobParentApplicationName value is current child process application
    std::string childAppToInject = KnobParentApplicationName.Value();
    std::string childApp(appArgv[0]);
    std::string::size_type index = childApp.find(childAppToInject);
    if (index == std::string::npos)
    {
        return FALSE;
    }

    //Set Pin's command line for child process
    INT pinArgc = 0;
    std::vector<const char*> pinArgv;

    std::string pin = KnobPinFullPath.Value() + "/pin";
    pinArgv.push_back(pin.c_str());
    pinArgv.push_back("-follow_execv");
    if (KnobProbeChild)
    {
        pinArgv.push_back("-probe"); // pin in probe mode
    }
    pinArgv.push_back("-t");
    std::string tool = KnobToolsFullPath.Value() + "/" + KnobParentToolName.Value();
    tool += ".dll";
    pinArgv.push_back(tool.c_str());
    if (KnobProbeGrandChild)
    {
        pinArgv.push_back("-probe_child");
        pinArgv.push_back("1");
    }
    pinArgv.push_back("-pin_path");
    std::string pin_path = KnobPinFullPath.Value();
    pinArgv.push_back(pin_path.c_str());
    pinArgv.push_back("-tools_path");
    std::string tool_path = KnobToolsFullPath.Value();
    pinArgv.push_back(tool_path.c_str());
    pinArgv.push_back("-check_process_id");
    pinArgv.push_back("1");
    pinArgv.push_back("-process_id");
    std::string decPidStr   = Int64ToDecimalString(pid).c_str();
    pinArgv.push_back(decPidStr.c_str());
    pinArgv.push_back("--");

    pinArgc = pinArgv.size();

    CHILD_PROCESS_SetPinCommandLine(cProcess, pinArgc, pinArgv.data());

    return TRUE;
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    std::cout << "In grand_parent_tool PinTool is probed " << decstr(PIN_IsProbeMode()) << std::endl;

    char currentDir[4000];
    if (NULL == getcwd( currentDir, sizeof(currentDir) ))
    {
        std::cerr << "couldnt get cwd" << std::endl;
        return -1;
    }

    currDirStr = currentDir;

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

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
