/*
 * Copyright (C) 2007-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
  Replace an original function with a custom function defined in the tool. The
  new function can have either the same or different signature from that of its
  original function.
*/

/* ===================================================================== */
#include "pin.H"
#include <iostream>
#include <limits.h>
#include <stdlib.h>



// On Windows, the 'long' native type is 4 bytes and not 8 bytes. Define the min and max values.
#if defined(TARGET_WINDOWS)
#define LONG_MAX_VALUE 0x7fffffffL
#define LONG_MIN_VALUE (-LONG_MAX_VALUE - 1)
#define ULONG_MAX_VALUE 0xffffffff
#elif defined(TARGET_LINUX)
#define LONG_MAX_VALUE LONG_MAX
#define LONG_MIN_VALUE LONG_MIN
#define ULONG_MAX_VALUE ULONG_MAX
#endif

/* ===================================================================== */
static short (*pf_bar)(short, short, unsigned short, long, long, unsigned long, short, short, unsigned short, long, long,
                       unsigned long);

/* ===================================================================== */
int Boo(CONTEXT* ctxt, AFUNPTR pf_Green)
{
    short ret = 0;

    PIN_CallApplicationFunction(ctxt, PIN_ThreadId(), CALLINGSTD_DEFAULT, pf_Green, NULL, PIN_PARG(short), &ret, PIN_PARG(short),
                                SHRT_MIN, PIN_PARG(short), SHRT_MAX, PIN_PARG(unsigned short), USHRT_MAX, PIN_PARG(long),
                                LONG_MIN_VALUE, PIN_PARG(long), LONG_MAX_VALUE, PIN_PARG(unsigned long), ULONG_MAX_VALUE,
                                PIN_PARG(short), SHRT_MIN, PIN_PARG(short), SHRT_MAX, PIN_PARG(unsigned short), USHRT_MAX,
                                PIN_PARG(long), LONG_MIN_VALUE, PIN_PARG(long), LONG_MAX_VALUE, PIN_PARG(unsigned long),
                                ULONG_MAX_VALUE, PIN_PARG_END());

    if (ret == 1)
        std::cout << " Correct return value " << std::endl;
    else
        std::cout << "ret = " << ret << std::endl;

    return ret;
}

/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(short), CALLINGSTD_DEFAULT, "Bar13", PIN_PARG(short), PIN_PARG(short),
                                     PIN_PARG(unsigned short), PIN_PARG(long), PIN_PARG(long), PIN_PARG(unsigned long),
                                     PIN_PARG(short), PIN_PARG(short), PIN_PARG(unsigned short), PIN_PARG(long), PIN_PARG(long),
                                     PIN_PARG(unsigned long), PIN_PARG_END());

        VOID* pf_Green;
        RTN rtn1 = RTN_FindByName(img, "Green13");
        if (RTN_Valid(rtn1))
            pf_Green = reinterpret_cast< VOID* >(RTN_Address(rtn1));
        else
        {
            std::cout << "Green13 cannot be found." << std::endl;
            exit(1);
        }

        RTN rtn = RTN_FindByName(img, "Bar13");
        if (RTN_Valid(rtn))
        {
            std::cout << "Replacing " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;

            pf_bar = (short (*)(short, short, unsigned short, long, long, unsigned long, short, short, unsigned short, long, long,
                                unsigned long))RTN_ReplaceSignature(rtn, AFUNPTR(Boo), IARG_PROTOTYPE, proto, IARG_CONTEXT,
                                                                    IARG_PTR, pf_Green, IARG_END);
        }
    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
