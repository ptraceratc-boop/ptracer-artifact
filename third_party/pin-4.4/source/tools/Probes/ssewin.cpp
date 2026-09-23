/*
 * Copyright (C) 2008-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <windows/pinrt_windows.h>








/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "ssewin.outfile", "specify trace file name");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool replaces SetSystemError().\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    std::cerr.flush();
    return -1;
}

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

typedef void (*FUNCPTR_SET_SYSTEM_ERROR)(WINDOWS::DWORD);

static void (*fp_setSystemError)(WINDOWS::DWORD);

ADDRINT mainImgEntry = 0;

/* ===================================================================== */

void ReplacedSetSystemErrorProbed(FUNCPTR_SET_SYSTEM_ERROR origFunc, WINDOWS::DWORD errorCode)
{
    // exercise heavy use of stack inside PinTool
    char useTheStack[0x8765];
    useTheStack[0] = 'a';

    fprintf(stderr, "SetSystemError(%d)\n", errorCode);

    origFunc(errorCode);
}

void ReplacedSetSystemErrorJit(CONTEXT* context, AFUNPTR origFunc, WINDOWS::DWORD errorCode)
{
    fprintf(stderr, "SetSystemError(%d)\n", errorCode);

    PIN_CallApplicationFunction(context, PIN_ThreadId(), CALLINGSTD_DEFAULT, origFunc, NULL, PIN_PARG(void), PIN_PARG(int),
                                errorCode, PIN_PARG_END());
}

VOID ImageLoad(IMG img, VOID* v)
{
    TraceFile << "Processing " << IMG_Name(img) << std::endl;
    TraceFile.flush();

    const char* name    = "SetSystemError";
    PROTO proto_funcptr = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, name, PIN_PARG(WINDOWS::DWORD), PIN_PARG_END());

    // Get the function pointer for the executable SetSystemError()
    RTN rtnSse = RTN_FindByName(img, "SetSystemError");
    if (RTN_Valid(rtnSse))
    {
        fprintf(stderr, "Found SetSystemError RTN\n");

        if (PIN_IsProbeMode())
            RTN_ReplaceSignatureProbed(rtnSse, AFUNPTR(ReplacedSetSystemErrorProbed), IARG_PROTOTYPE, proto_funcptr,
                                       IARG_ORIG_FUNCPTR, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        else
            RTN_ReplaceSignature(rtnSse, AFUNPTR(ReplacedSetSystemErrorJit), IARG_PROTOTYPE, proto_funcptr, IARG_CONTEXT,
                                 IARG_ORIG_FUNCPTR, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    }
}

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    IMG_AddInstrumentFunction(ImageLoad, 0);

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
