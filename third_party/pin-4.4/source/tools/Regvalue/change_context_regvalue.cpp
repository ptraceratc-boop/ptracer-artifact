/*
 * Copyright (C) 2013-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <fstream>
#include <cstring>
#include <cassert>
#include "context_utils.h"
#include "register_modification_utils.h"





/////////////////////
// GLOBAL VARIABLES
/////////////////////

// A knob for defining which context to test. One of:
// 1. default   - regular CONTEXT passed to the analysis routine using IARG_CONTEXT.
// 2. partial   - partial CONTEXT passed to the analysis routine using IARG_PARTIAL_CONTEXT.
KNOB< std::string > KnobTestContext(KNOB_MODE_WRITEONCE, "pintool", "testcontext", "default",
                                    "specify which context to test. One of default|partial.");

// A knob for defining the output file name
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "change_context_regvalue.out",
                                   "specify output file name");

// std::ofstream object for handling the output
std::ofstream OutFile;

/////////////////////
// ANALYSIS FUNCTIONS
/////////////////////

static void ReplaceChangeRegs(CONTEXT* ctxt)
{
    OutFile << "Context values before they are changed" << std::endl << std::flush;
    StoreContext(ctxt);
    PrintStoredRegisters(OutFile);
    ModifyContext(ctxt);
}

static void ReplaceChangeRegsAndExecute(CONTEXT* ctxt, ADDRINT executeAtAddr)
{
    ReplaceChangeRegs(ctxt);
    PIN_SetContextReg(ctxt, REG_INST_PTR, executeAtAddr);
    PIN_ExecuteAt(ctxt);
}

/////////////////////
// CALLBACKS
/////////////////////

#define SAVEAPPPOINTERS_FN_NAME "SaveAppPointers"


static VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        // Find the application's modified values in memory
        RTN SaveAppPointersRtn = RTN_FindByName(img, SAVEAPPPOINTERS_FN_NAME);
        assert(RTN_Valid(SaveAppPointersRtn));
        RTN_Open(SaveAppPointersRtn);
        RTN_InsertCall(SaveAppPointersRtn, IPOINT_BEFORE, MAKE_AFUNPTR(ToolSaveAppPointers), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 4, IARG_FUNCARG_ENTRYPOINT_VALUE, 5, IARG_PTR, &OutFile, IARG_END);
        RTN_Close(SaveAppPointersRtn);

        // Find the placeholder for PIN_ExecuteAt
        RTN executeAtRtn = RTN_FindByName(img, "ExecuteAt");
        assert(RTN_Valid(executeAtRtn));

        // Instrument ChangeRegs
        RTN changeRegsRtn = RTN_FindByName(img, "ChangeRegs");
        assert(RTN_Valid(changeRegsRtn));
        RTN_Open(changeRegsRtn);
        if (KnobTestContext.Value() == "default")
        {
            RTN_InsertCall(changeRegsRtn, IPOINT_AFTER, MAKE_AFUNPTR(ReplaceChangeRegsAndExecute), IARG_CONTEXT, IARG_ADDRINT,
                           RTN_Address(executeAtRtn), IARG_END);
        }
        else if (KnobTestContext.Value() == "partial")
        {
            REGSET regsin  = GetTestRegset();
            REGSET regsout = GetTestRegset();
            RTN_InsertCall(changeRegsRtn, IPOINT_AFTER, MAKE_AFUNPTR(ReplaceChangeRegs), IARG_PARTIAL_CONTEXT, &regsin, &regsout,
                           IARG_END);
        }
        else
        {
            OutFile << "ERROR: Unknown context requested for testing: " << KnobTestContext.Value() << std::endl;
            PIN_ExitApplication(2); // never returns
        }
        RTN_Close(changeRegsRtn);

        // Instrument SaveRegsToMem
        RTN SaveRegsToMemRtn = RTN_FindByName(img, "SaveRegsToMem");
        assert(RTN_Valid(SaveRegsToMemRtn));
        RTN_Open(SaveRegsToMemRtn);
        RTN_InsertCall(SaveRegsToMemRtn, IPOINT_AFTER, MAKE_AFUNPTR(CheckToolModifiedValues), IARG_CONTEXT, IARG_PTR, &OutFile,
                       IARG_END);
        RTN_Close(SaveRegsToMemRtn);
    }
}

static VOID Fini(INT32 code, VOID* v) { OutFile.close(); }

/////////////////////
// MAIN FUNCTION
/////////////////////

int main(int argc, char* argv[])
{
    // Initialize Pin
    PIN_InitSymbolsAlt(EXPORT_SYMBOLS);
    PIN_Init(argc, argv);

    // Open the output file
    OutFile.open(KnobOutputFile.Value().c_str());

    // Add instrumentation
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Start running the application
    PIN_StartProgram(); // never returns

    return 0;
}
