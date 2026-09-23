/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <fstream>
#include <iostream>
#include "pin.H"

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output file name");

static std::ostream* out = nullptr;

#define DELETE_OUT \
    if (&std::cerr != out) delete out

#ifdef TARGET_WINDOWS
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif
#define EXTERNC extern "C"

EXTERNC EXPORT VOID analysis_function_iarg() { *out << __PRETTY_FUNCTION__ << " called" << std::endl; }

EXTERNC EXPORT VOID analysis_function_func_ptr() { *out << __PRETTY_FUNCTION__ << " called" << std::endl; }

EXTERNC EXPORT VOID analysis_function_func_name() { *out << __PRETTY_FUNCTION__ << " called" << std::endl; }

EXTERNC EXPORT VOID analysis_function_another_func() { *out << __PRETTY_FUNCTION__ << " called" << std::endl; }

EXTERNC EXPORT VOID analysis_function_use_dladdr() { *out << __PRETTY_FUNCTION__ << " called" << std::endl; }

VOID Trace(TRACE trace, VOID* v)
{
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Example 1: Use the function name as the actual name for log inline
        BBL_InsertCall(bbl, IPOINT_BEFORE, MAKE_AFUNPTR(analysis_function_func_name), IARG_END);

        // Example 2: Use a function pointer and specify a name manually
        BBL_InsertCall(bbl, IPOINT_BEFORE, MAKE_AFUNPTR(analysis_function_func_ptr),
                       FUNC_NAME_IARG_STRING("analysis_function_func_ptr"), IARG_END);

        // Example 3: Use a different name for logging
        BBL_InsertCall(bbl, IPOINT_BEFORE, MAKE_AFUNPTR(analysis_function_iarg), FUNC_NAME_IARG(analysis_function_another_func),
                       IARG_END);

        // Example 4: Use the IARG excplicitly.
        BBL_InsertCall(bbl, IPOINT_BEFORE, MAKE_AFUNPTR(analysis_function_iarg), IARG_FUNC_NAME, "analysis_function_iarg",
                       IARG_END);

        // Example 5: Try without IARG or MACRO (use dladdr)
        BBL_InsertCall(bbl, IPOINT_BEFORE, MAKE_AFUNPTR(analysis_function_use_dladdr), IARG_END);
    }
}

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool tests log-inline IARG and MACROs " << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* =====================================================================
 * Called upon program finish
 * ===================================================================== */
VOID Fini(int, VOID* v)
{
    *out << "Finished" << std::endl;
    DELETE_OUT;
    return;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Initialize the output stream.
    const std::string fileName = KnobOutputFile.Value();
    out                        = (fileName.empty()) ? &std::cerr : new std::ofstream(fileName.c_str());
    if (NULL == out || out->fail())
    {
        std::cerr << "TOOL ERROR: Unable to open " << fileName << " for writing." << std::endl;
        return 1;
    }

    // Register instruction to be called to instrument instructions
    TRACE_AddInstrumentFunction(Trace, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // start the program, never returns
    PIN_StartProgram();

    return 0;
}