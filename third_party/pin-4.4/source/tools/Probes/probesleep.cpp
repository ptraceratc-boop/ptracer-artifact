/*
 * Copyright (C) 2006-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool replaces sleep().
//

#include "pin.H"
#include <iostream>
#include "tool_macros.h"




#define SLEEP_SUFFIX

#ifdef TARGET_WINDOWS
typedef void(__stdcall* sleepfunc_t)(int);
#else
typedef void (*sleepfunc_t)(int);
#endif

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

sleepfunc_t pf_sleep;

/* ===================================================================== */
/* Replacement Functions */
/* ===================================================================== */

#ifdef TARGET_WINDOWS
__stdcall
#endif
    void
    SleepProbe(int b)
{
    std::cerr << "SleepProbe: calling sleep" << std::endl;

    if (pf_sleep)
    {
        (pf_sleep)(b);
    }
}

/* ===================================================================== */

#if defined(TARGET_WINDOWS)

// Returns TRUE if baseName matches tail of imageName. Comparison is case-insensitive.
// Parameters:
//   imageName  image file name in either form with extension
//   baseName   image base name with extension (e.g. kernel32.dll)
BOOL MatchedImageName(const std::string& imageName, const std::string& baseName)
{
    if (imageName.size() >= baseName.size())
    {
        return strcasecmp(imageName.c_str() + imageName.size() - baseName.size(), baseName.c_str()) == 0;
    }
    return FALSE;
}

#endif

/* ===================================================================== */

// Called every time a new image is loaded
// Look for routines that we want to probe

VOID ImageLoad(IMG img, VOID* v)
{
#if defined(TARGET_WINDOWS)
    RTN sleepRtn = RTN_Invalid();
    // Look for Sleep only in kernel32.dll
    if (MatchedImageName(IMG_Name(img), "kernel32.dll"))
    {
        sleepRtn = RTN_FindByName(img, C_MANGLE("Sleep"));
    }
#else
    RTN sleepRtn = RTN_FindByName(img, C_MANGLE("sleep") SLEEP_SUFFIX);
#endif

    if (RTN_Valid(sleepRtn))
    {
        if (RTN_IsSafeForProbedReplacement(sleepRtn))
        {
            pf_sleep = (sleepfunc_t)RTN_ReplaceProbed(sleepRtn, AFUNPTR(SleepProbe));

            std::cerr << "Inserted probe for Sleep:" << IMG_Name(img) << std::endl;
        }
    }
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
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
