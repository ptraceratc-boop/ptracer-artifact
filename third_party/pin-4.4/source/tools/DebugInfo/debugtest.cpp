/*
 * Copyright (C) 2008-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include "pin.H"






std::string FILE_TO_FIND = "hello";

void InstImage(IMG img, void* v)
{
    if (!IMG_IsMainExecutable(img)) return;

    std::cout << "Image name = " << IMG_Name(img) << std::endl << std::flush;

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            RTN_Open(rtn);

            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
            {
                int lineno;
                std::string srcfile = "";
                PIN_GetSourceLocation(INS_Address(ins), NULL, &lineno, &srcfile);
                if (srcfile.find(FILE_TO_FIND) != srcfile.npos)
                {
                    fprintf(stderr, "passed\n");
                    break;
                }
            }
            RTN_Close(rtn);
        }
    }
}

int main(int argc, char** argv)
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        std::cerr << "usage..." << std::endl;
        return EXIT_FAILURE;
    }

    IMG_AddInstrumentFunction(InstImage, 0);

    PIN_StartProgram();
    return EXIT_FAILURE;
}
