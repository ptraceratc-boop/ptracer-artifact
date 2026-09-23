/*
 * Copyright (C) 2013-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// This tool verifies the correctness of passing a register by reference to an analysis routine.

#include <iostream>
#include <fstream>
#include <cassert>
#include "register_modification_utils.h"
#include "regvalue_utils.h"






/////////////////////
// GLOBAL VARIABLES
/////////////////////

// A knob for defining the output file name
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "reg_reference.out",
                                   "specify file name for reg_reference output");

// A knob for defining which register reference to use. One of:
// 1. default   - regular REG_REFERENCE passed to the analysis routine using IARG_REG_REFERENCE.
// 2. const     - const REG_REFERENCE passed to the analysis routine using IARG_REG_CONST_REFERENCE.
KNOB< std::string > KnobTestReference(KNOB_MODE_WRITEONCE, "pintool", "testreference", "default",
                                      "specify which context to test. One of default|const.");

// std::ofstream object for handling the output.
std::ofstream OutFile;

/////////////////////
// UTILITY FUNCTIONS
/////////////////////

static int Usage()
{
    std::cerr << "This tool verifies the correctness of passing a register by reference to an analysis routine." << std::endl
         << std::endl
         << KNOB_BASE::StringKnobSummary() << std::endl;
    return 1;
}

/////////////////////
// ANALYSIS FUNCTIONS
/////////////////////

static void ChangeRegAfter(REG reg, PINTOOL_REGISTER* val)
{
    // Verify that the register's value is correct
    UINT size = REG_Size(reg);
    CompareValues(val, GetAppRegisterValue(reg), size, OutFile);
    OutFile << "Original value of " << REG_StringShort(reg) << " = " << GetAppRegisterValue(reg) << std::endl << std::flush;

    // Assign a new value for the register
    const UINT64* newval = reinterpret_cast< const UINT64* >(GetToolRegisterValue(reg));
    UINT qwords          = size >> 3;
    if (0 == qwords) ++qwords;
    AssignNewPinRegisterValue(val, newval, qwords);
}

/////////////////////
// CALLBACKS
/////////////////////

#define SAVEAPPPOINTERS_FN_NAME "SaveAppPointers"

static VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        // Instrument ChangeRegs
        RTN changeRegsRtn = RTN_FindByName(img, "ChangeRegs");
        assert(RTN_Valid(changeRegsRtn));
        RTN_Open(changeRegsRtn);
        REGSET regset = GetTestRegset();
        for (INS ins = RTN_InsHead(changeRegsRtn); INS_Valid(ins); ins = INS_Next(ins))
        {
            REG reg = INS_RegW(ins, 0);
            if (REGSET_Contains(regset, reg))
            {
                if (KnobTestReference.Value() == "default")
                {
                    INS_InsertCall(ins, IPOINT_AFTER, MAKE_AFUNPTR(ChangeRegAfter), IARG_UINT32, reg, IARG_REG_REFERENCE, reg,
                                   IARG_END);
                }
                else if (KnobTestReference.Value() == "const")
                {
                    INS_InsertCall(ins, IPOINT_AFTER, MAKE_AFUNPTR(ChangeRegAfter), IARG_UINT32, reg, IARG_REG_CONST_REFERENCE,
                                   reg, IARG_END);
                }
                else
                {
                    OutFile << "ERROR: Unknown reference requested for testing: " << KnobTestReference.Value() << std::endl;
                    PIN_ExitApplication(2); // never returns
                }
            }
        }
        RTN_Close(changeRegsRtn);

        // When using the regular (R/W) reference, also check for correct modification of the registers.
        if (KnobTestReference.Value() == "default")
        {
            // Find the application's modified values in memory
            RTN SaveAppPointersRtn = RTN_FindByName(img, SAVEAPPPOINTERS_FN_NAME);
            assert(RTN_Valid(SaveAppPointersRtn));
            RTN_Open(SaveAppPointersRtn);
            RTN_InsertCall(SaveAppPointersRtn, IPOINT_BEFORE, MAKE_AFUNPTR(ToolSaveAppPointers), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                           IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                           IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE, 5, IARG_END);
            RTN_Close(SaveAppPointersRtn);

            // Instrument SaveRegsToMem
            RTN SaveRegsToMemRtn = RTN_FindByName(img, "SaveRegsToMem");
            assert(RTN_Valid(SaveRegsToMemRtn));
            RTN_Open(SaveRegsToMemRtn);
            RTN_InsertCall(SaveRegsToMemRtn, IPOINT_AFTER, MAKE_AFUNPTR(CheckToolModifiedValues), IARG_CONTEXT, IARG_PTR,
                           &OutFile, IARG_END);
            RTN_Close(SaveRegsToMemRtn);
        }
    }
}

static VOID Fini(INT32 code, VOID* v) { OutFile.close(); }

/////////////////////
// MAIN FUNCTION
/////////////////////

int main(int argc, char* argv[])
{
    // Initialize pin
    PIN_InitSymbolsAlt(EXPORT_SYMBOLS);
    if (PIN_Init(argc, argv)) return Usage();

    OutFile.open(KnobOutputFile.Value().c_str());

    // Register Instruction to be called to instrument instructions
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
