/*
 * Copyright (C) 2007-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>








/*
 * This test puts a probe in very short function and checks how Pin
 * moves thread IP, if it fails on probe
 */

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "probe_tool.outfile", "specify file name");

std::ofstream TraceFile;
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool tests probe replacement.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

void Do_Nothing()
{
    // defining 'x' as volatile to avoid ICX generating set-but-no-used error
    volatile int x = 0;
    while (1)
    {
        x++;
        --x;
    }
    (void)x;
}

UINT32 threadCounter = 0;

VOID AttachedThreadStart(VOID* sigmask, VOID* v)
{
    TraceFile << "Thread counter is updated to " << std::dec << ++threadCounter << std::endl;
}

int PinReady(unsigned int numOfThreads) { return (threadCounter == numOfThreads) ? 1 : 0; }

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn = RTN_FindByName(img, "ShortFunc");
    if (RTN_Valid(rtn))
    {
        RTN_ReplaceProbed(rtn, AFUNPTR(Do_Nothing));
    }

    rtn = RTN_FindByName(img, "ShortFunc2");
    if (RTN_Valid(rtn))
    {
        RTN_ReplaceProbed(rtn, AFUNPTR(Do_Nothing));
    }

    rtn = RTN_FindByName(img, "ThreadsReady");
    if (RTN_Valid(rtn))
    {
        if (!RTN_IsSafeForProbedReplacement(rtn))
        {
            fprintf(stderr, "Can't replace ThreadsReady\n");
            exit(-1);
        }
        RTN_ReplaceProbed(rtn, AFUNPTR(PinReady));
    }
}

/* ===================================================================== */

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
    PIN_AddThreadAttachProbedFunction(AttachedThreadStart, 0);
    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
