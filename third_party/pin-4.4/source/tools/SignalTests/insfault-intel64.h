/*
 * Copyright (C) 2011-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef INSFAULT_INTEL64_H
#define INSFAULT_INTEL64_H

#include <cstdint>

// The assembly file sets these to the expected values for each register.
//
extern "C" uint64_t ExpectedPC;
extern "C" uint64_t ExpectedR8;
extern "C" uint64_t ExpectedR9;
extern "C" uint64_t ExpectedR10;
extern "C" uint64_t ExpectedR11;
extern "C" uint64_t ExpectedR12;
extern "C" uint64_t ExpectedR13;
extern "C" uint64_t ExpectedR14;
extern "C" uint64_t ExpectedR15;
extern "C" uint64_t ExpectedRDI;
extern "C" uint64_t ExpectedRSI;
extern "C" uint64_t ExpectedRBP;
extern "C" uint64_t ExpectedRBX;
extern "C" uint64_t ExpectedRDX;
extern "C" uint64_t ExpectedRAX;
extern "C" uint64_t ExpectedRCX;
extern "C" uint64_t ExpectedRSP;
extern "C" uint64_t ExpectedEFLAGS;

// Mask of bits to check in EFLAGS:
//
//  AC, OF, DF, TF, SF, ZF, AF, PF, CF
//
static const uint64_t EFLAGS_MASK = 0x40dd5;

#endif
