/*
 * Copyright (C) 2004-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/* This example demonstrates finding a function by name on Windows.      */
/* ===================================================================== */

#include "pin.H"
#include <windows/pinrt_windows.h>
#include <iostream>
#include <fstream>






/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "w_malloctrace.out", "specify trace file name");

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool produces a trace of calls to RtlAllocateHeap.";
    std::cerr << std::endl << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */

VOID Before(CHAR* name, WINDOWS::HANDLE hHeap, WINDOWS::DWORD dwFlags, WINDOWS::DWORD dwBytes)
{
    TraceFile << "Before: " << name << "(" << std::hex << hHeap << ", " << dwFlags << ", " << dwBytes << ")" << std::dec << std::endl;
}

VOID After(CHAR* name, ADDRINT ret) { TraceFile << "After: " << name << "  returns " << std::hex << ret << std::dec << std::endl; }

/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */

VOID Image(IMG img, VOID* v)
{
    // Walk through the symbols in the symbol table.
    //
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        std::string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);

        //  Find the RtlAllocHeap() function.
        if (undFuncName == "RtlAllocateHeap")
        {
            RTN allocRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));

            if (RTN_Valid(allocRtn))
            {
                // Instrument to print the input argument value and the return value.
                RTN_Open(allocRtn);

                RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR)Before, IARG_ADDRINT, "RtlAllocateHeap",
                               IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE,
                               2, IARG_END);
                RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR)After, IARG_ADDRINT, "RtlAllocateHeap",
                               IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);

                RTN_Close(allocRtn);
            }
        }
    }
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID* v) { TraceFile.close(); }

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize pin & symbol manager
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Write to a file since std::cout and std::cerr maybe closed by the application
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    // Register Image to be called to instrument functions.
    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
