/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"

#include <cstdlib>
#include <iostream>
#include <string>

#include "tool_macros.h"

KNOB< std::string > KnobReturnReg(KNOB_MODE_WRITEONCE, "pintool", "return_reg", "gdx",
                                  "register to use with IARG_RETURN_REGS: gdx or gflags");

static const char* const TargetFunctionName = "IargReturnRegsProbeTarget";
static const ADDRINT kExpectedValue         = 0x1234;

static REG GetReturnReg()
{
    const std::string regName = KnobReturnReg.Value();
    if (regName == "gdx") return REG_GDX;
    if (regName == "gflags") return REG_GFLAGS;

    std::cerr << "Unknown return_reg value: " << regName << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    std::exit(1);
}

ADDRINT ReturnValueForGpr()
{
    std::cout << "IARG_RETURN_REGS probe analysis executed" << std::endl << std::flush;
    return kExpectedValue;
}

VOID CheckGdxValue(ADDRINT val)
{
    if (val == kExpectedValue)
    {
        std::cout << "GDX value correctly set to " << std::hex << kExpectedValue << " by IARG_RETURN_REGS" << std::endl
                  << std::flush;
    }
    else
    {
        std::cerr << "ERROR: GDX value is " << std::hex << val << ", expected " << std::hex << kExpectedValue << std::endl
                  << std::flush;
        std::exit(1);
    }
}

VOID ImageLoad(IMG img, VOID*)
{
    if (!IMG_IsMainExecutable(img)) return;

    RTN rtn = RTN_FindByName(img, C_MANGLE(TargetFunctionName));
    if (!RTN_Valid(rtn))
    {
        std::cerr << "Cannot find " << TargetFunctionName << std::endl;
        std::exit(1);
    }

    const REG returnReg = GetReturnReg();

    // Insert the probe that sets GDX (or other reg) via IARG_RETURN_REGS
    if (!RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(ReturnValueForGpr), IARG_RETURN_REGS, returnReg, IARG_END))
    {
        std::cerr << "Cannot insert probe before " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;
        std::exit(1);
    }
    std::cout << "Inserted IARG_RETURN_REGS probe with " << REG_StringShort(returnReg) << std::endl << std::flush;

    // Only add the check for GDX in the positive test (not for gflags)
    if (returnReg == REG_GDX)
    {
        if (!RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(CheckGdxValue), IARG_REG_VALUE, REG_GDX, IARG_END))
        {
            std::cerr << "Cannot insert GDX check probe before " << RTN_Name(rtn) << std::endl;
            std::exit(1);
        }
        std::cout << "Inserted GDX value check probe" << std::endl << std::flush;
    }
}

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) return 1;

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();
    return 0;
}