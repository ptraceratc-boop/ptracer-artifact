/*
 * Copyright (C) 2024-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
using std::cerr;
using std::endl;
using std::hex;
using std::ios;
using std::string;

/* ===================================================================== */
/* Names of malloc and free */
/* ===================================================================== */
#define MALLOC "malloc"
#define FREE "free"

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "malloctrace_probe.out", "specify trace file name");

/* ===================================================================== */

/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */

VOID Arg1Before(CHAR* name, ADDRINT size) { TraceFile << name << "(" << size << ")" << endl; }

VOID MallocAfter(ADDRINT ret) { TraceFile << "  returns " << ret << endl; }

/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */

VOID Image(IMG img, VOID* v)
{
    // Instrument the malloc() and free() functions.  Print the input argument
    // of each malloc() or free(), and the return value of malloc().
    //
    //  Find the malloc() function.
    RTN mallocRtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(mallocRtn))
    {
        // Instrument malloc() to print the input argument value and the return value.
        RTN_InsertCallProbed(mallocRtn, IPOINT_BEFORE, (AFUNPTR)Arg1Before, IARG_ADDRINT, MALLOC, IARG_FUNCARG_ENTRYPOINT_VALUE,
                             0, IARG_END);

        PROTO proto_malloc_after = PROTO_Allocate(PIN_PARG(void*), CALLINGSTD_DEFAULT, MALLOC, PIN_PARG(size_t), PIN_PARG_END());
        if (RTN_InsertCallProbed(mallocRtn, IPOINT_AFTER, (AFUNPTR)MallocAfter, IARG_PROTOTYPE, proto_malloc_after,
                                 IARG_FUNCRET_EXITPOINT_VALUE, IARG_END))
        {
            ASSERTX(RTN_IsSafeForProbedInsertion(mallocRtn));
            std::cout << "Inserting calls before/after malloc in " << IMG_Name(img) << std::endl;
        }
        else
        {
            std::cout << "Cannot insert calls around " << RTN_Name(mallocRtn) << "() in " << IMG_Name(img) << std::endl;
            exit(1);
        }

    }

    // Find the free() function.
    RTN freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(freeRtn))
    {
        ASSERTX(RTN_IsSafeForProbedInsertion(freeRtn));
        // Instrument free() to print the input argument value.
        RTN_InsertCallProbed(freeRtn, IPOINT_BEFORE, (AFUNPTR)Arg1Before, IARG_ADDRINT, FREE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                             IARG_END);
    }
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    cerr << "This tool produces a trace of calls to malloc." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize pin & symbol manager
    PIN_InitSymbolsAlt(SYMBOL_INFO_MODE(UINT32(IFUNC_SYMBOLS) | UINT32(DEBUG_OR_EXPORT_SYMBOLS)));
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Write to a file since cout and cerr maybe closed by the application
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << hex;
    TraceFile.setf(ios::showbase);

    // Register Image to be called to instrument functions.
    IMG_AddInstrumentFunction(Image, 0);

    // Never returns
    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
