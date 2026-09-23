/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This test check interface PIN_UndecorateSymbolName() demangling API.
 * The test reads the symbols from the application image, demangling selected symbols,
 * and compares the results to reference that is generated locally, outside this test.
 */

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>

#include "pin.H"






KNOB< std::string > KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "o", "undecorate.out", "Name for log file");
KNOB< BOOL > KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "v", "0", "Verbose output to log file");
KNOB< BOOL > KnobFullNameCheck(KNOB_MODE_WRITEONCE, "pintool", "full_name_check", "0",
                               "Check functions full signature match, not name only");

static std::ofstream out;

// Define the names we expect to demangle
struct DemangledNames
{
    std::string fullName;
    std::string noArgsName;

    DemangledNames(const std::string& full_name, const std::string& no_args_name) : fullName(full_name), noArgsName(no_args_name)
    {}

    BOOL operator==(const DemangledNames& demangledName) const
    {
        if (KnobFullNameCheck && !(fullName == demangledName.fullName))
        {
            return FALSE;
        }
        return (noArgsName == demangledName.noArgsName);
    }
};

static std::vector< DemangledNames > testNames;

VOID RemoveSpaces(std::string& demangledName, std::string& demangledNameNoParams)
{
    demangledName.erase(std::remove(demangledName.begin(), demangledName.end(), ' '), demangledName.end());
    demangledNameNoParams.erase(std::remove(demangledNameNoParams.begin(), demangledNameNoParams.end(), ' '),
                                demangledNameNoParams.end());
    return;
}

// Auxiliary function that generates the references for the symbols names from two files
// created by make command - 'demangled_names.txt' and 'demangled_names_no_args.txt'
VOID InitDemangledNamesVector()
{
    std::ifstream demangeldNamesFile("demangled_names.txt");
    std::ifstream demangeldNamesNoArgsFile("demangled_names_no_args.txt");

    if (!demangeldNamesFile.is_open() || !demangeldNamesNoArgsFile.is_open())
    {
        std::cout << "Error: Unable to open demangled_names.txt OR demangled_names_no_args.txt" << std::endl;
        PIN_ExitApplication(1);
    }

    std::string full_name;
    std::string no_args_name;

    // we read both files line by line and store them in vector
    while (std::getline(demangeldNamesFile, full_name) && std::getline(demangeldNamesNoArgsFile, no_args_name))
    {
        DemangledNames current_name(full_name, no_args_name);
        testNames.push_back(current_name);
    }

    demangeldNamesFile.close();
    demangeldNamesNoArgsFile.close();

    // print the names for verification
    out << "--- Symbols Names Reference (extracted from application binary) ---" << std::endl;
    for (const auto& name : testNames)
    {
        out << "full name:\t" << name.fullName << std::endl;
        out << "no args name:\t" << name.noArgsName << std::endl << std::endl;
    }
    out << std::endl << "Reference vector contains " << testNames.size() << " symbols names" << std::endl << std::endl << std::endl;

    return;
}

VOID ImageLoad(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img))
    {
        return;
    }

    out << "--- Symbols Names (extracted via PIN_UndecorateSymbolName) ---" << std::endl << std::endl;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            std::string rtnName = RTN_Name(rtn);

            std::string demangledName         = PIN_UndecorateSymbolName(rtnName, UNDECORATION_COMPLETE);
            std::string demangledNameNoParams = PIN_UndecorateSymbolName(rtnName, UNDECORATION_NAME_ONLY);

            if (rtnName.find("MyMethod") != std::string::npos || rtnName.find("Foo") != std::string::npos ||
                rtnName.find("my_operator") != std::string::npos || rtnName.find("MyBar") != std::string::npos)
            {
                out << "Full mangled name             : " << rtnName << std::endl;
                out << "Full demangled name signature : " << demangledName << std::endl;
                out << "Demangled name w/o parameters : " << demangledNameNoParams << std::endl << std::endl;

                BOOL matched = FALSE;
                for (const auto& name : testNames)
                {
                    RemoveSpaces(demangledName, demangledNameNoParams);

                    if (KnobFullNameCheck && !(demangledName == name.fullName))
                    {
                        // if full name check is enabled and full name is not matching,
                        // then no need to check name-only without arguments
                        continue;
                    }
                    if (demangledNameNoParams == name.noArgsName)
                    {
                        matched = TRUE;
                        break;
                    }
                }

                if (!matched)
                {
                    std::cout << std::endl << "Error in demangling: " << std::endl;
                    std::cout << "Mangled name: " << rtnName << std::endl;
                    std::cout << "Demangled name: " << demangledName << std::endl;
                    std::cout << "Demangled name, no parameters: " << demangledNameNoParams << std::endl;
                    PIN_ExitApplication(1);
                }
                continue;
            }

            // Otherwise just demangle the name both ways but normally don't bother to print them.
            // We can't easily tell what the results should be, but throwing a lot more
            // names at our demangler must be a good thing to do, and this should include
            // all the names from the standard C++ runtime, is a reasonable stress test.
            if (KnobVerbose)
            {
                out << rtnName << " => " << std::endl;
                out << "   " << demangledName << std::endl;
                out << "   " << demangledNameNoParams << std::endl << std::endl;
            }
        }
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    out.open(KnobOutput.Value().c_str());
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // create test reference
    InitDemangledNamesVector();

    // Never returns
    PIN_StartProgram();

    return 0;
}
