/*
 * Copyright (C) 2004-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>

UINT64 icount = 0;

static VOID PrintContext(CONTEXT* ctxt)
{
#if defined(TARGET_IA32) || defined(TARGET_IA32E)
    std::cout << "gax:   " << ctxt->_gax << std::endl;
    std::cout << "gbx:   " << ctxt->_gbx << std::endl;
    std::cout << "gcx:   " << ctxt->_gcx << std::endl;
    std::cout << "gdx:   " << ctxt->_gdx << std::endl;
    std::cout << "gsi:   " << ctxt->_gsi << std::endl;
    std::cout << "gdi:   " << ctxt->_gdi << std::endl;
    std::cout << "gbp:   " << ctxt->_gbp << std::endl;
    std::cout << "sp:    " << ctxt->_stack_ptr << std::endl;

#if defined(TARGET_IA32E)
    std::cout << "r8:    " << ctxt->_r8 << std::endl;
    std::cout << "r9:    " << ctxt->_r9 << std::endl;
    std::cout << "r10:   " << ctxt->_r10 << std::endl;
    std::cout << "r11:   " << ctxt->_r11 << std::endl;
    std::cout << "r12:   " << ctxt->_r12 << std::endl;
    std::cout << "r13:   " << ctxt->_r13 << std::endl;
    std::cout << "r14:   " << ctxt->_r14 << std::endl;
    std::cout << "r15:   " << ctxt->_r15 << std::endl;
#endif

    std::cout << "ss:    " << ctxt->_ss << std::endl;
    std::cout << "cs:    " << ctxt->_cs << std::endl;
    std::cout << "ds:    " << ctxt->_ds << std::endl;
    std::cout << "es:    " << ctxt->_es << std::endl;
    std::cout << "fs:    " << ctxt->_fs << std::endl;
    std::cout << "gs:    " << ctxt->_gs << std::endl;
    std::cout << "gflags:" << ctxt->_gflags << std::endl;

    std::cout << "mxcsr: " << ctxt->_fxsave._mxcsr << std::endl;

#endif
}

VOID ShowContext(VOID* ip, VOID* handle, ADDRINT gax)
{
    CONTEXT ctxt;

    // Capture the context. This must be done first before some floating point
    // registers have been overwritten
    PIN_MakeContext(handle, &ctxt);

    static bool first = false;

    if (first)
    {
        std::cout << "ip:    " << ip << std::endl;

        PrintContext(&ctxt);

        std::cout << std::endl;
    }

#if defined(TARGET_IA32) || defined(TARGET_IA32E)
    ASSERTX(gax == ctxt._gax);
#endif
}

VOID Trace(TRACE tr, VOID* v)
{
    TRACE_InsertCall(tr, IPOINT_BEFORE, MAKE_AFUNPTR(ShowContext), IARG_INST_PTR, IARG_CONTEXT, IARG_REG_VALUE, REG_GAX,
                     IARG_END);
}

int main(int argc, char* argv[])
{
    std::cout << std::hex;
    std::cout.setf(std::ios::showbase);

    PIN_Init(argc, argv);

    TRACE_AddInstrumentFunction(Trace, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
