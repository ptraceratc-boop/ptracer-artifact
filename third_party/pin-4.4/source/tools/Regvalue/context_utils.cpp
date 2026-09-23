/*
 * Copyright (C) 2013-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "context_utils.h"

/////////////////////
// API FUNCTIONS IMPLEMENTATION
/////////////////////

void StoreContext(const CONTEXT* ctxt)
{
    std::vector< REG > regs = GetTestRegs();
    int numOfRegs      = regs.size();
    for (int r = 0; r < numOfRegs; ++r)
    {
        REG reg = regs[r];
        PIN_GetContextRegval(ctxt, reg, GetRegval(reg));
    }
}

void ModifyContext(CONTEXT* ctxt)
{
    std::vector< REG > regs = GetTestRegs();
    int numOfRegs      = regs.size();
    for (int r = 0; r < numOfRegs; ++r)
    {
        REG reg = regs[r];
        PIN_SetContextRegval(ctxt, reg, GetToolRegisterValue(reg));
    }
}
