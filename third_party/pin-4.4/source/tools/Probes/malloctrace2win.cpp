/*
 * Copyright (C) 2007-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
  Similar to Probes/malloctrace.C, but puts replacement functions in the
  application namespace. Works in probed and jitted mode.

  Application functions can no longer be safely called from replacement
  routines via a function pointer in JIT mode.  They must be called
  using PIN_CallApplicationFunction().  Application functions cannot
  be called from a callback in JIT mode at all.

  Therefore, this test case has been restructured to separate Probe
  mode and JIT mode processing.  There is very little common code.
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
namespace WIND
{
#include <windows.h>
}








/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "malloctrace2.outfile", "specify trace file name");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool inserts a user-written version of malloc() and free() into the application.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    std::cerr.flush();
    return -1;
}

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

BOOL replaced     = FALSE;
BOOL kernel_found = FALSE;
BOOL crtl_found   = FALSE;

/* ===================================================================== */
/* Replacement routines for probe mode */
/* ===================================================================== */

void* (*fp_mallocFun)(size_t size);
void* MallocProbeWrapper(int size)
{
    void* res = fp_mallocFun(size);

    fprintf(stderr, "malloc(%d) = %p\n", size, res);
    fflush(stderr);

    return res;
}

void (*fp_freeFun)(void* p);
void FreeProbeWrapper(void* p)
{
    fp_freeFun(p);

    fprintf(stderr, "free(%p)\n", p);
    fflush(stderr);
}

/* ===================================================================== */
/* Replacement routines for JIT mode */
/* ===================================================================== */

void* MallocJitWrapper(CONTEXT* ctxt, AFUNPTR pf_malloc, size_t size)
{
    void* res;

    fprintf(stderr, "Calling malloc(%d)\n", (int)size);
    fflush(stderr);

    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pf_malloc, NULL, PIN_PARG(void*), &res,
                                PIN_PARG(size_t), size, PIN_PARG_END());

    fprintf(stderr, "malloc(%d) = %p\n", (int)size, res);
    fflush(stderr);

    return res;
}

void FreeJitWrapper(CONTEXT* ctxt, AFUNPTR pf_free, void* p)
{
    fprintf(stderr, "Calling free(%p)\n", p);
    fflush(stderr);

    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pf_free, NULL, PIN_PARG(void), PIN_PARG(void*), p,
                                PIN_PARG_END());

    fprintf(stderr, "free(%p)\n", p);
    fflush(stderr);
}

/* ===================================================================== */
/* Instrumentation Routines */
/* ===================================================================== */
// Look for routines that we want to replace

VOID ReplaceRtnsProbed(IMG img)
{
    // Replace malloc and free in application libc with probe mode wrappers
    // in mallocwrappers.dll.
    RTN mallocRtn = RTN_FindByName(img, "malloc");
    ASSERTX(RTN_Valid(mallocRtn));

    AFUNPTR mallocimpl = RTN_ReplaceProbed(mallocRtn, AFUNPTR(MallocProbeWrapper));
    ASSERTX(mallocimpl);
    fp_mallocFun = (void* (*)(size_t))mallocimpl;

    RTN freeRtn = RTN_FindByName(img, "free");
    ASSERTX(RTN_Valid(freeRtn));

    AFUNPTR freeimpl = RTN_ReplaceProbed(freeRtn, AFUNPTR(FreeProbeWrapper));
    ASSERTX(freeimpl);
    fp_freeFun = (void (*)(void*))freeimpl;
}

VOID ReplaceRtnsJit(IMG img)
{
    // Replace malloc and free in application libc with jit mode wrappers
    RTN mallocRtn = RTN_FindByName(img, "malloc");
    ASSERTX(RTN_Valid(mallocRtn));

    PROTO protoMalloc = PROTO_Allocate(PIN_PARG(void*), CALLINGSTD_DEFAULT, "malloc", PIN_PARG(size_t), PIN_PARG_END());

    RTN_ReplaceSignature(mallocRtn, AFUNPTR(MallocJitWrapper), IARG_PROTOTYPE, protoMalloc, IARG_CONTEXT, IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);

    RTN freeRtn = RTN_FindByName(img, "free");
    ASSERTX(RTN_Valid(freeRtn));

    PROTO protoFree = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "free", PIN_PARG(void*), PIN_PARG_END());

    RTN_ReplaceSignature(freeRtn, AFUNPTR(FreeJitWrapper), IARG_PROTOTYPE, protoFree, IARG_CONTEXT, IARG_ORIG_FUNCPTR,
                         IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
}

/*
 * Return TRUE if baseName matches tail of imageName. Comparison is case-insensitive.
 * @param[in]  imageName  image file name in either form with extension
 * @param[in]  baseName   image base name with extension (e.g. kernel32.dll)
 */
static BOOL CmpBaseImageName(const std::string& imageName, const std::string& baseName)
{
    if (imageName.size() >= baseName.size())
    {
        return strcasecmp(imageName.c_str() + imageName.size() - baseName.size(), baseName.c_str()) == 0;
    }
    return FALSE;
}

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
//
VOID ImageLoad(IMG img, VOID* v)
{
    TraceFile << "Processing " << IMG_Name(img) << std::endl;
    TraceFile.flush();

    if (PIN_IsProbeMode())
    {
        if (CmpBaseImageName(IMG_Name(img), "kernel32.dll"))
        {
            TraceFile << "Found " << IMG_Name(img) << std::endl;
            TraceFile.flush();

            kernel_found = TRUE;
        }

        if (CmpBaseImageName(IMG_Name(img), "msvcr80.dll") || CmpBaseImageName(IMG_Name(img), "msvcr90.dll") ||
            CmpBaseImageName(IMG_Name(img), "msvcr100.dll") || CmpBaseImageName(IMG_Name(img), "msvcr110.dll") ||
            CmpBaseImageName(IMG_Name(img), "msvcr120.dll") || CmpBaseImageName(IMG_Name(img), "msvcrt.dll"))
        {
            TraceFile << "Found " << IMG_Name(img) << std::endl;
            TraceFile.flush();

            crtl_found = TRUE;
        }

        if (!replaced && kernel_found && crtl_found)
        {
            replaced = TRUE;
            ReplaceRtnsProbed(img);
        }
    }
    else // JIT mode!
    {
        if (CmpBaseImageName(IMG_Name(img), "msvcr80.dll") || CmpBaseImageName(IMG_Name(img), "msvcr90.dll") ||
            CmpBaseImageName(IMG_Name(img), "msvcr100.dll") || CmpBaseImageName(IMG_Name(img), "msvcr110.dll") ||
            CmpBaseImageName(IMG_Name(img), "msvcr120.dll") || CmpBaseImageName(IMG_Name(img), "msvcrt.dll"))
        {
            TraceFile << "Found " << IMG_Name(img) << std::endl;
            TraceFile.flush();

            ReplaceRtnsJit(img);
        }
    }
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    if (PIN_IsProbeMode())
    {
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
