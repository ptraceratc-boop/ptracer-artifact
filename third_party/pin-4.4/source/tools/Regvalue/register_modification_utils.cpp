/*
 * Copyright (C) 2013-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cassert>
#include <map>
#include "register_modification_utils.h"
#include "context_utils.h"





/////////////////////
// GLOBAL VARIABLES
/////////////////////

// The regvalue_app application stores the register values after the ChangeRegs routine completes.
// The tool can then inspect these values to check whether value replacement was successful.
// This map holds pointers to these stored values.
static std::map< REG, void* > applicationStoredValues;

/////////////////////
// UTILITY FUNCTIONS
/////////////////////

bool CheckModifiedValues(std::ostream& ost)
{
    bool success              = true;
    const std::vector< REG >& regs = GetTestRegs();
    int numOfRegs             = regs.size();
    for (int r = 0; r < numOfRegs; ++r)
    {
        REG reg = regs[r];
        success &= CompareValues(applicationStoredValues[reg], GetToolRegisterValue(reg), REG_Size(reg), ost);
    }
    return success;
}

/////////////////////
// API FUNCTIONS IMPLEMENTATION
/////////////////////

void CheckToolModifiedValues(CONTEXT* ctxt, void* /* std::ostream* */ ostptr)
{
    std::ostream& ost = *((std::ostream*)ostptr);
    ost << "Context values after being changed" << std::endl << std::flush;
    StoreContext(ctxt);
    PrintStoredRegisters(ost);
    if (!CheckModifiedValues(ost))
    {
        ost << "ERROR: values mismatch" << std::endl << std::flush;
        PIN_ExitApplication(1); // never returns
    }
}

void ToolSaveAppPointers(void* gprptr, void* stptr, void* xmmptr, void* ymmptr, void* zmmptr, void* opmaskptr,
                         void* /* std::ostream* */ ostptr)
{
    const std::vector< REG >& regs = GetTestRegs();
    int numberOfTestRegs      = regs.size();
    for (int r = 0; r < numberOfTestRegs; ++r)
    {
        if (REG_is_gr(regs[r]))
        {
            applicationStoredValues[regs[r]] = gprptr;
        }
        else if (REG_is_st(regs[r]))
        {
            applicationStoredValues[regs[r]] = stptr;
        }
        else if (REG_is_xmm(regs[r]))
        {
            applicationStoredValues[regs[r]] = xmmptr;
        }
        else if (REG_is_ymm(regs[r]))
        {
            applicationStoredValues[regs[r]] = ymmptr;
        }
        else if (REG_is_zmm(regs[r]))
        {
            applicationStoredValues[regs[r]] = zmmptr;
        }
        else if (REG_is_k_mask(regs[r]))
        {
            applicationStoredValues[regs[r]] = opmaskptr;
        }
        else
        {
            *((std::ostream*)ostptr) << "Test configuration includes an invalid register: " << REG_StringShort(regs[r]) << std::endl;
            PIN_ExitApplication(100);
        }
    }
}
