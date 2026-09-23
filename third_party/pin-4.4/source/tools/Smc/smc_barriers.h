/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file smc_barriers.h
 *
 * Inline serializing instructions used in SMC (Self-Modifying Code) patterns
 * and JIT compilers, provided as test infrastructure for Pin SMC tests.
 *
 *   smc_barrier_cpuid()
 *   smc_barrier_serialize()
 */

#ifndef SMC_BARRIERS_H
#define SMC_BARRIERS_H

#include "../Utils/supports_serialize.h"

#if defined(_MSC_VER)
/* Forward-declare MSVC intrinsics directly.  These are compiler built-ins;
 * #pragma intrinsic enables them without requiring <intrin.h> to be
 * reachable in the include path. */
extern "C" void __cpuid(int[4], int);
#pragma intrinsic(__cpuid)
extern "C" void _serialize(void);
#pragma intrinsic(_serialize)
#endif

static inline void smc_barrier_cpuid(void)
{
#if defined(_MSC_VER)
    int info[4];
    __cpuid(info, 0);
    (void)info;
#else
    int eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0) : "memory");
    (void)eax;
    (void)ebx;
    (void)ecx;
    (void)edx;
#endif
}

static inline void smc_barrier_serialize(void)
{
    /* 0F 01 E8 = SERIALIZE */
#if defined(_MSC_VER)
    _serialize(); /* MSVC intrinsic (VS 2019 16.8+) */
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(".byte 0x0F, 0x01, 0xE8" ::: "memory");
#else
#error "smc_barrier_serialize: no implementation for this compiler"
#endif
}

#endif /* SMC_BARRIERS_H */
