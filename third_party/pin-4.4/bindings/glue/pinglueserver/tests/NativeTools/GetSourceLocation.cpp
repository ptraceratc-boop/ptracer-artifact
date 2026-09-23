/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <string>
#include "pin.H"
using std::cerr;
using std::cout;
using std::endl;
using std::ios;
using std::string;

string FILE_TO_FIND = "hello";

std::ofstream out;

void InstImage(IMG img, void* v)
{
    if (!IMG_IsMainExecutable(img)) return;

    out << "Image name = " << IMG_Name(img) << endl;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            RTN_Open(rtn);

            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
            {
                int column, lineno;
                string srcfile = "";
                PIN_GetSourceLocation(INS_Address(ins), &column, &lineno, &srcfile);
                if (srcfile.find(FILE_TO_FIND) != srcfile.npos)
                {
                    out << "column: " << column << ", line: " << lineno << ", srcfile: " << srcfile << endl;
                }
            }
            RTN_Close(rtn);
        }
    }
}

int main(int argc, char** argv)
{
    PIN_InitSymbolsAlt(SYMBOL_INFO_MODE(UINT32(IFUNC_SYMBOLS) | UINT32(DEBUG_OR_EXPORT_SYMBOLS)));

    if (PIN_Init(argc, argv))
    {
        cerr << "usage..." << endl;
        return EXIT_FAILURE;
    }

    out.open("GetSourceLocation.out");
    out.setf(ios::showbase);
    IMG_AddInstrumentFunction(InstImage, 0);

    PIN_StartProgram();

    return 0;
}
