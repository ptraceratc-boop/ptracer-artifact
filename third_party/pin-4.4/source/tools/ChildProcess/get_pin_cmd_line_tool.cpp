/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Test tool for CHILD_PROCESS_GetPinCommandLine API (runs in parent process).
 *
 * In the FollowChild callback this tool:
 *  1. Reads the original Pin command line and verifies it contains the expected tool and flags
 *  2. Builds a new command line replacing this tool with -child_tool
 *  3. Sets it via CHILD_PROCESS_SetPinCommandLine
 *  4. Reads it again and verifies element-by-element it matches what was set
 *
 * The child process then runs with the replacement tool (child_log_tool).
 */

#include "pin.H"
#include <fstream>
#include <string>
#include <vector>

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "get_pin_cmd_line_tool.out", "output file");
KNOB< std::string > KnobChildTool(KNOB_MODE_WRITEONCE, "pintool", "child_tool", "", "full path of tool for child process");
KNOB< std::string > KnobChildToolOutput(KNOB_MODE_WRITEONCE, "pintool", "child_tool_output", "", "output file for child tool");
KNOB< BOOL > KnobRunChildUnderPin(KNOB_MODE_WRITEONCE, "pintool", "run_child_under_pin", "1",
                                  "run the child process under Pin after checking its command line");

std::ofstream OutFile;

static const CHAR* MmanKnobs[] = {"-pin_memory_range", "-pin_memory_size", "-cc_memory_size", "-restrict_memory",
                                  "-enforce_pin_range_allocations"};

BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    // Step 1: Get the original Pin command line
    INT origArgc;
    const CHAR* const* origArgv;
    CHILD_PROCESS_GetPinCommandLine(cProcess, &origArgc, &origArgv);

    OutFile << "Original Pin command line:";
    for (INT i = 0; i < origArgc; i++)
    {
        OutFile << " " << origArgv[i];
    }
    OutFile << std::endl;

    // Verify original contains expected elements
    bool foundPin         = (origArgc > 0 && std::string(origArgv[0]).find("pin") != std::string::npos);
    bool foundTool        = false;
    bool foundFollowExecv = false;
    bool foundMmanKnob    = false;
    bool foundDelimiter   = (origArgc > 0 && std::string(origArgv[origArgc - 1]) == "--");

    for (INT i = 0; i < origArgc; i++)
    {
        std::string arg(origArgv[i]);
        if (arg == "-t" && i + 1 < origArgc && std::string(origArgv[i + 1]).find("get_pin_cmd_line_tool") != std::string::npos)
        {
            foundTool = true;
        }
        if (arg == "-follow_execv")
        {
            foundFollowExecv = true;
        }
        for (size_t knobIndex = 0; knobIndex < sizeof(MmanKnobs) / sizeof(MmanKnobs[0]); knobIndex++)
        {
            if (arg == MmanKnobs[knobIndex])
            {
                foundMmanKnob = true;
            }
        }
    }

    ASSERT(foundPin && foundTool && foundDelimiter, "Original command line missing expected tool or delimiter");
    ASSERT(foundFollowExecv, "Original command line missing follow_execv flag");
    ASSERT(!foundMmanKnob, "Original command line contains an MMAN knob");
    OutFile << "MMAN knobs removed from follow-child callback: yes" << std::endl;

    // Step 2: Build new command line keeping all pin flags, replacing only the tool section.
    // Copy everything before "-t" (pin exe + pin flags), then append new tool section.
    INT tIndex = -1;
    for (INT i = 0; i < origArgc; i++)
    {
        if (std::string(origArgv[i]) == "-t")
        {
            tIndex = i;
            break;
        }
    }

    ASSERT(0 <= tIndex, "Original command line missing '-t' flag");

    std::vector< std::string > argStorage;
    for (INT i = 0; i < tIndex; i++)
    {
        argStorage.push_back(origArgv[i]);
    }
    argStorage.push_back("-t");
    argStorage.push_back(KnobChildTool.Value());
    argStorage.push_back("-o");
    argStorage.push_back(KnobChildToolOutput.Value());
    argStorage.push_back("--");

    std::vector< const char* > newArgv;
    for (size_t i = 0; i < argStorage.size(); i++)
    {
        newArgv.push_back(argStorage[i].c_str());
    }

    // Step 3: Set the new command line
    CHILD_PROCESS_SetPinCommandLine(cProcess, static_cast< INT >(newArgv.size()), newArgv.data());

    // Step 4: Get modified command line and compare element-by-element
    INT modArgc;
    const CHAR* const* modArgv;
    CHILD_PROCESS_GetPinCommandLine(cProcess, &modArgc, &modArgv);

    OutFile << "Modified Pin command line:";
    for (INT i = 0; i < modArgc; i++)
    {
        OutFile << " " << modArgv[i];
    }
    OutFile << std::endl;

    // Verify: first part (pin flags) unchanged, then new tool section appended
    INT expectedArgc = static_cast< INT >(newArgv.size());
    bool matches     = (modArgc == expectedArgc);
    for (INT i = 0; i < modArgc && matches; i++)
    {
        matches = (std::string(modArgv[i]) == argStorage[static_cast< size_t >(i)]);
    }

    OutFile << "Modified command line matches expected: " << (matches ? "yes" : "no") << std::endl;

    return KnobRunChildUnderPin.Value();
}

VOID Fini(INT32 code, VOID* v)
{
    OutFile << "get_pin_cmd_line_tool Fini" << std::endl;
    OutFile.close();
}

int main(INT32 argc, CHAR** argv)
{
    if (PIN_Init(argc, argv)) return -1;

    OutFile.open(KnobOutputFile.Value().c_str());

    PIN_AddFollowChildProcessFunction(FollowChild, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}