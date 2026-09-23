/*
 * Copyright (C) 2020-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "asm_macros.h"

.data
zero: .float 0

.text
.global NAME(FptagInitFunc)
NAME(FptagInitFunc):
        finit
        ret

.global NAME(FldzFunc)
NAME(FldzFunc):
        fldz
        ret

.global NAME(FldInfFunc)
NAME(FldInfFunc):
        fldpi
#if defined(TARGET_IA32E)
        fdivl zero
#else
        fdivs zero
#endif
        ret

.global NAME(DoFnstenv)
NAME(DoFnstenv):
        BEGIN_STACK_FRAME
        mov PARAM1,GAX_REG
        fnstenv (GAX_REG)
        END_STACK_FRAME
        ret

.global NAME(FstpFunc)
NAME(FstpFunc):
        fstp %st(0)
        ret
