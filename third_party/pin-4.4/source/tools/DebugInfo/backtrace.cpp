/*
 * Copyright (C) 2016-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <execinfo.h>
#include <iostream>
#include "pin.H"




extern "C" void qux() { std::cout << "qux" << std::endl; }

extern "C" void baz()
{
    void* buf[128];
    int nptrs = backtrace(buf, sizeof(buf) / sizeof(buf[0]));
    ASSERTX(nptrs > 0);
    char** bt = backtrace_symbols(buf, nptrs);
    ASSERTX(NULL != bt);
    for (int i = 0; i < nptrs; i++)
    {
        std::cout << bt[i] << std::endl;
    }
    free(bt);
}

extern "C" void bar()
{
    baz();
    qux();
}

extern "C" void foo()
{
    bar();
    qux();
}

void InstImage(IMG img, void* v)
{
    if (IMG_IsMainExecutable(img))
    {
        foo();
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
