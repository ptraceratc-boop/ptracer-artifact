/*
 * Copyright (C) 2012-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 *  This test checks bogus symbol size
 */

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>

#include "pin.H"






KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "elf_symsize.out", "specify output file name");

std::ofstream outfile;

VOID ImageLoad(IMG img, void* v)
{
    if (!IMG_IsMainExecutable(img))
    {
        outfile << "Ignoring image: " << IMG_Name(img) << std::endl;
        return;
    }

    outfile << "Parsing image: " << IMG_Name(img) << std::endl;
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        outfile << "Symbol " << SYM_Name(sym) << " address 0x" << std::hex << SYM_Address(sym) << std::endl;

        RTN rtn = RTN_FindByName(img, SYM_Name(sym).c_str());
        if (!RTN_Valid(rtn))
        {
            outfile << "Routine not found, continue..." << std::endl;
            continue;
        }

        outfile << "Routine " << RTN_Name(rtn) << " address 0x" << std::hex << RTN_Address(rtn) << " size 0x" << std::hex << RTN_Size(rtn)
                << std::endl;
    }
}

VOID Fini(INT32 code, VOID* v)
{
    outfile << "Symbol test passed successfully" << std::endl;
    outfile.close();
}
/* ===================================================================== */
/* Print Help Message.                                                   */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This is the invocation pintool" << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

/* ===================================================================== */
/* Main.                                                                 */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) return Usage();

    outfile.open(KnobOutputFile.Value().c_str());
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    return 0;
}

/* ================================================================== */
