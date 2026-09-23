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
VOID* Probe_Malloc_IA32(FUNCPTR_MALLOC orgFuncptr, size_t arg0, ADDRINT returnIp, ADDRINT esp, ADDRINT ebp)
{
    std::cout << "Probe_Malloc_IA32 (" << std::hex << (ADDRINT)orgFuncptr << ", " << std::hex << arg0 << ", " << std::hex << returnIp << "," << std::hex
         << esp << ", " << std::hex << ebp << ")" << std::endl
         << std::flush;

    ASSERTX(esp <= 0xffffffff);

    VOID* v = orgFuncptr(arg0);
    return v;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    const char* name = "malloc";

    RTN rtn = RTN_FindByName(img, name);
    if (RTN_Valid(rtn) && RTN_IsSafeForProbedReplacement(rtn))
    {
        PROTO proto_funcptr =
            PROTO_Allocate(PIN_PARG(WINDOWS::LPVOID), CALLINGSTD_DEFAULT, name, PIN_PARG(size_t), PIN_PARG_END());

        std::cout << "Replacing " << name << " in " << IMG_Name(img) << std::endl << std::flush;

        RTN_ReplaceSignatureProbed(rtn, AFUNPTR(Probe_Malloc_IA32), IARG_PROTOTYPE, proto_funcptr, IARG_ORIG_FUNCPTR,
                                   IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_RETURN_IP, IARG_REG_VALUE, LEVEL_BASE::REG_ESP,
                                   IARG_REG_VALUE, LEVEL_BASE::REG_EBP, IARG_END);

    }
    else if (RTN_Valid(rtn))
    { // This is workaround for mantis 4588. When mantis is handled this code need to be addressed.
        std::cout << "Replacement not safe" << std::endl << std::flush;
    }
}
/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}
/* ===================================================================== */
/* eof */
/* ===================================================================== */
