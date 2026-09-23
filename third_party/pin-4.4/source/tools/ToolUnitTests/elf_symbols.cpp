/*
 * Copyright (C) 2010-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 *  This test checks SYM_Address() interface on Linux
 */

#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>

#include "pin.H"






KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "elf_symbols.out", "specify output file name");

std::ofstream outfile;

VOID ImageLoad(IMG img, void* v)
{
    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
    {
        std::string symPureName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
        if (symPureName == "fopen")
        {
            RTN fopenRtn = RTN_FindByAddress(SYM_Address(sym));
            if (!RTN_Valid(fopenRtn))
            {
                std::cerr << "Routine " << symPureName << " is not found at address 0x" << std::hex << SYM_Address(sym) << std::endl;
                exit(-1);
            }
            else
            {
                outfile << "Routine " << SYM_Name(sym) << " is found at address 0x" << std::hex << SYM_Address(sym) << std::endl;
            }
        }
        if (symPureName == "malloc")
        {
            RTN mallocRtn = RTN_FindByAddress(SYM_Address(sym));
            if (!RTN_Valid(mallocRtn))
            {
                std::cerr << "Routine " << symPureName << " is not found at address 0x" << std::hex << SYM_Address(sym) << std::endl;
                exit(-1);
            }
            else
            {
                outfile << "Routine " << SYM_Name(sym) << " is found at address 0x" << std::hex << SYM_Address(sym) << std::endl;
            }
        }
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
