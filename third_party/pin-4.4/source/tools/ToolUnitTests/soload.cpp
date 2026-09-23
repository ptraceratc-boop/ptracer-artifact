/*
 * Copyright (C) 2005-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <fstream>
#include "pin.H"




#define ONE "one.so"
#define TWO "two.so"


std::ofstream out("soload.out");

VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_Name(img).find(ONE) != std::string::npos) out << "Loading one.so" << std::endl;
    if (IMG_Name(img).find(TWO) != std::string::npos) out << "Loading two.so" << std::endl;
}

VOID ImageUnload(IMG img, VOID* v)
{
    if (IMG_Name(img).find(ONE) != std::string::npos) out << "unloading one.so" << std::endl;
    if (IMG_Name(img).find(TWO) != std::string::npos) out << "unloading two.so" << std::endl;
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);
    IMG_AddUnloadFunction(ImageUnload, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
