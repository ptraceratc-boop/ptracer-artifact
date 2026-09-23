/*
 * Copyright (C) 2006-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
  Replace an original function with a custom function defined in the tool. The
  new function can have either the same or different signature from that of its
  original function.
*/

/* ===================================================================== */
#include "pin.H"
#include <iostream>

#include <windows/pinrt_windows.h>








typedef VOID* (*FUNCPTR_MALLOC)(size_t);

/* ===================================================================== */
VOID* Jit_Malloc_IA32(CONTEXT* context, FUNCPTR_MALLOC orgFuncptr, size_t arg0, ADDRINT returnIp, ADDRINT esp, ADDRINT ebp)
{
    std::cout << "Jit_Malloc_IA32 (" << std::hex << (ADDRINT)orgFuncptr << ", " << std::hex << arg0 << ", " << std::hex << returnIp << "," << std::hex
         << esp << ", " << std::hex << ebp << ")" << std::endl
         << std::flush;

    VOID* ret;

    PIN_CallApplicationFunction(context, PIN_ThreadId(), CALLINGSTD_DEFAULT, AFUNPTR(orgFuncptr), NULL, PIN_PARG(void*), &ret,
                                PIN_PARG(size_t), arg0, PIN_PARG_END());

    std::cout << "Jit_Malloc_IA32: ret = " << std::hex << (ADDRINT)ret << std::dec << std::endl << std::flush;
    return ret;
}

/* ===================================================================== */
VOID* Probe_Malloc_IA32(FUNCPTR_MALLOC orgFuncptr, size_t arg0, ADDRINT returnIp, ADDRINT esp, ADDRINT ebp)
{
    std::cout << "Probe_Malloc_IA32 (" << std::hex << (ADDRINT)orgFuncptr << ", " << std::hex << arg0 << ", " << std::hex << returnIp << "," << std::hex
         << esp << ", " << std::hex << ebp << ")" << std::endl
         << std::flush;

    VOID* ret;

    ret = orgFuncptr(arg0);

    std::cout << "Probe_Malloc_IA32: ret = " << std::hex << (ADDRINT)ret << std::dec << std::endl << std::flush;
    return ret;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    const char* name = "malloc";

    RTN rtn = RTN_FindByName(img, name);
    if (RTN_Valid(rtn))
    {
        PROTO proto_malloc =
            PROTO_Allocate(PIN_PARG(WINDOWS::LPVOID), CALLINGSTD_DEFAULT, name, PIN_PARG(size_t), PIN_PARG_END());

        std::cout << "Replacing " << name << " in " << IMG_Name(img) << std::endl << std::flush;

        if (!PIN_IsProbeMode())
        {
            RTN_ReplaceSignature(rtn, AFUNPTR(Jit_Malloc_IA32), IARG_PROTOTYPE, proto_malloc, IARG_CONTEXT, IARG_ORIG_FUNCPTR,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_RETURN_IP, IARG_REG_VALUE, LEVEL_BASE::REG_ESP,
                                 IARG_REG_VALUE, LEVEL_BASE::REG_EBP, IARG_END);
        }
        else if (RTN_IsSafeForProbedReplacement(rtn))
        {
            RTN_ReplaceSignatureProbed(rtn, AFUNPTR(Probe_Malloc_IA32), IARG_PROTOTYPE, proto_malloc, IARG_ORIG_FUNCPTR,
                                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_RETURN_IP, IARG_REG_VALUE, LEVEL_BASE::REG_ESP,
                                       IARG_REG_VALUE, LEVEL_BASE::REG_EBP, IARG_END);
        }
        else
        { // This is workaround for mantis 4588. When mantis is handled this code need to be addressed.
            std::cout << "Replacement not safe" << std::endl << std::flush;
        }

    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    if (PIN_IsProbeMode())
        PIN_StartProgramProbed();
    else
        PIN_StartProgram();

    return 0;
}
/* ===================================================================== */
/* eof */
/* ===================================================================== */
