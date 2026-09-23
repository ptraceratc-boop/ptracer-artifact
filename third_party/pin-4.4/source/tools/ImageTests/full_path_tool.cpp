/*
 * Copyright (C) 2013-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <assert.h>






KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "full_path.out", "specify file name");

std::ofstream TraceFile;

static VOID ImageLoad(IMG img, VOID* data) { TraceFile << "%s is loaded\n" << IMG_Name(img) << std::endl; }

int main(int argc, char** argv)
{
    if (!PIN_Init(argc, argv))
    {
        TraceFile.open(KnobOutputFile.Value().c_str());

        PIN_InitSymbols();

        IMG_AddInstrumentFunction(ImageLoad, 0);

        PIN_StartProgramProbed();
    }

    return 1;
}
