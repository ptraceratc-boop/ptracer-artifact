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








/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;
static void (*pf_dn)();

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "probe9.outfile", "specify file name");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool tests probe replacement.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

void Foo_Function()
{
    if (pf_dn)
    {
        (*pf_dn)();

        TraceFile << "Shorty replacement." << std::endl;
    }
}

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    std::cout << "Processing " << IMG_Name(img) << std::endl;

    RTN rtn = RTN_FindByName(img, "shorty");
    if (RTN_Valid(rtn))
    {
        if (!RTN_IsSafeForProbedReplacement(rtn))
        {
            TraceFile << "Cannot replace " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;
            exit(1);
        }

        pf_dn = (void (*)())RTN_ReplaceProbed(rtn, AFUNPTR(Foo_Function));

        TraceFile << "Inserted probe for shorty:" << IMG_Name(img) << std::endl;
    }

    std::cout << "Completed " << IMG_Name(img) << std::endl;
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

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
