/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// Test an analysis routine that does an fxsave/fxstore.

#include <iostream>
#include <stdio.h>
#include "pin.H"



void my_print(int x)
{
#if defined(TARGET_IA32) || defined(TARGET_IA32E)
    static char buffer[2048];
    static char* aligned_bufp = reinterpret_cast< char* >(((reinterpret_cast< ADDRINT >(buffer) + 16) >> 4) << 4);

#if defined(PIN_GNU_COMPATIBLE)
    std::cerr << "Pin GNU compatible" << std::endl;
    asm volatile("fxsave %0\n\t"
                 "emms"
                 : "=m"(*aligned_bufp));
#else
    __asm {
            push eax
            
            mov  eax, aligned_bufp
            fxsave [eax]
            
            pop eax
          }
#endif
#endif

    std::cerr << "my_print: " << x << std::endl;
    double y = x * 0.33445;
    std::cerr << "Done initializing y" << std::endl;
    std::cerr << y << std::endl;
    std::cerr << "Done with my_print" << std::endl;

#if defined(TARGET_IA32) || defined(TARGET_IA32E)
#if defined(PIN_GNU_COMPATIBLE)
    asm volatile("fxrstor %0" ::"m"(*aligned_bufp));
#else
    __asm {
            push eax
            
            mov  eax, aligned_bufp
            fxrstor [eax]
            
            pop eax
          }
#endif
#endif
}

VOID ImageLoad(IMG img, VOID* v)
{
    RTN rtn;

    rtn = RTN_FindByName(img, "print");
    if (RTN_Valid(rtn))
    {
        RTN_Open(rtn);

        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)my_print, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);

        RTN_Close(rtn);
    }
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();
    return 0;
}
