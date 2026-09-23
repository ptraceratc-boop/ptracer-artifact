/*
 * Copyright (C) 2021-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Check handling of very long symbol names (more than 1024 bytes)
 * provided by RTN_Name(), SYM_Name() and PIN_UndecorateSymbolName() APIs.
 */

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>

#include "pin.H"





KNOB< std::string > KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "o", "sym_long_name.out", "Name for log file");

static std::ofstream out;

VOID ImageLoad(IMG img, VOID* v)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            std::string rtnName = RTN_Name(rtn);
            if (rtnName.find("VeryVeryVery") != std::string::npos)
            {
                std::string demangledName         = PIN_UndecorateSymbolName(rtnName, UNDECORATION_COMPLETE);
                std::string demangledNameNoParams = PIN_UndecorateSymbolName(rtnName, UNDECORATION_NAME_ONLY);

                out << "Mangled routine name: " << rtnName << std::endl;
                out << "Full demangled routine name: " << demangledName << std::endl;
                out << "Demangled routine name w/o parameters: " << demangledNameNoParams << std::endl;

                SYM rtnSym     = RTN_Sym(rtn);
                std::string symName = SYM_Name(rtnSym);

                demangledName         = PIN_UndecorateSymbolName(symName, UNDECORATION_COMPLETE);
                demangledNameNoParams = PIN_UndecorateSymbolName(symName, UNDECORATION_NAME_ONLY);

                out << "Mangled symbol name: " << symName << std::endl;
                out << "Full demangled symbol name: " << demangledName << std::endl;
                out << "Demangled symbol name w/o parameters: " << demangledNameNoParams << std::endl;
            }
        }
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    out.open(KnobOutput.Value().c_str());

    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
