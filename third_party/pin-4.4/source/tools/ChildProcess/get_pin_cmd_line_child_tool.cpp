/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Child tool for get_pin_cmd_line test (runs in child process).
 *
 * Verifies that the Pin command line it was launched with matches what
 * the parent tool (get_pin_cmd_line_tool) set via CHILD_PROCESS_SetPinCommandLine.
 * Expected: [pin_exe, <pin_flags...>, "-t", <get_pin_cmd_line_child_tool_path>, "-o", <output_path>, "--"]
 */

#include "pin.H"
#include <fstream>
#include <string>

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "get_pin_cmd_line_child_tool.out", "output file");

static std::ofstream OutFile;

VOID Fini(INT32 code, VOID* v)
{
    OutFile << "get_pin_cmd_line_child_tool ran successfully" << std::endl;
    OutFile.close();
}

int main(INT32 argc, CHAR** argv)
{
    // Save argc/argv before PIN_Init for command line verification
    int SavedArgc    = argc;
    char** SavedArgv = argv;

    if (PIN_Init(argc, argv)) return -1;

    OutFile.open(KnobOutputFile.Value().c_str());

    PIN_AddFiniFunction(Fini, 0);

    // Verify the Pin command line matches what get_pin_cmd_line_tool set.
    // Expected: [pin_exe, <pin_flags...>, "-t", get_pin_cmd_line_child_tool_path, "-o", output_path, "--"]
    // Find the Pin portion (up to and including "--")
    int pinArgc = 0;
    for (int i = 0; i < SavedArgc; i++)
    {
        if (std::string(SavedArgv[i]) == "--")
        {
            pinArgc = i + 1;
            break;
        }
    }

    OutFile << "Pin command line in child:";
    for (int i = 0; i < pinArgc; i++)
    {
        OutFile << " " << SavedArgv[i];
    }
    OutFile << std::endl;

    // Verify: pin_exe present, ends with "--", and has at least the tool section
    bool argcMatches = (pinArgc >= 6) && (std::string(SavedArgv[pinArgc - 1]) == "--");
    OutFile << "Pin command line argc matches expected: " << (argcMatches ? "yes" : "no") << std::endl;

    // Verify tool section: last 5 elements before app args should be "-t tool -o output --"
    // and first element should contain "pin"
    bool contentMatches = argcMatches && (std::string(SavedArgv[0]).find("pin") != std::string::npos) &&
                          (std::string(SavedArgv[pinArgc - 5]) == "-t") &&
                          (std::string(SavedArgv[pinArgc - 4]).find("get_pin_cmd_line_child_tool") != std::string::npos) &&
                          (std::string(SavedArgv[pinArgc - 3]) == "-o") &&
                          (std::string(SavedArgv[pinArgc - 2]) == KnobOutputFile.Value()) &&
                          (std::string(SavedArgv[pinArgc - 1]) == "--");

    OutFile << "Pin command line content matches expected: " << (contentMatches ? "yes" : "no") << std::endl;

    PIN_StartProgram();

    return 0;
}
