/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include "tool_macros.h"








/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;
static void (*pf_dn)();
static void (*pf_nd)();

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "probe1.outfile", "specify file name");

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
    if (pf_dn)
    {
        (*pf_dn)();

        TraceFile << "Doing nothing." << std::endl;
    }
}

void Nothing_Doing()
{
    if (pf_nd)
    {
        (*pf_nd)();

        TraceFile << "Nothing doing." << std::endl;
    }
}

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    std::cout << "Processing " << IMG_Name(img) << std::endl;

    RTN rtn = RTN_FindByName(img, C_MANGLE("do_nothing"));
    if (RTN_Valid(rtn))
    {
        if (!RTN_IsSafeForProbedReplacement(rtn))
        {
            TraceFile << "Cannot replace " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;
            exit(1);
        }

        pf_dn = (void (*)())RTN_ReplaceProbed(rtn, AFUNPTR(Do_Nothing));

        TraceFile << "Inserted probe for do_nothing:" << IMG_Name(img) << std::endl;
    }

    rtn = RTN_FindByName(img, C_MANGLE("nothing_doing"));
    if (RTN_Valid(rtn))
    {
        if (!RTN_IsSafeForProbedReplacement(rtn))
        {
            TraceFile << "Cannot replace " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;
            exit(1);
        }

        pf_nd = (void (*)())RTN_ReplaceProbed(rtn, AFUNPTR(Nothing_Doing));

        TraceFile << "Inserted probe for nothing_doing:" << IMG_Name(img) << std::endl;
    }

    std::cout << "Completed " << IMG_Name(img) << std::endl;
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbolsAlt(EXPORT_SYMBOLS);

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
