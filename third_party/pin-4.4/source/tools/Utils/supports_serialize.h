/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file supports_serialize.h
 *
 * Portable inline C/C++ implementation of the SERIALIZE-support detection
 * function.  Uses CPUID leaf 7, subleaf 0, EDX bit 14 per Intel SDM vol.2.
 *
 * Compatible with C99/C11 and C++11 or later.
 * May be included from both plain-C translation units and C++ translation units.
 *
 * Public interface:
 *   int  ProcessorSupportsSerialize(void)   — returns non-zero if supported
 */

#ifndef SUPPORTS_SERIALIZE_H
#define SUPPORTS_SERIALIZE_H

/* ===================================================================== */
/* Compiler-specific CPUID declarations                                  */
/* ===================================================================== */

#if defined(_MSC_VER)
#ifdef __cplusplus
extern "C"
{
#endif
    void __cpuidex(int[4], int, int);
#ifdef __cplusplus
}
#endif
#pragma intrinsic(__cpuidex)
#endif /* _MSC_VER */

/* ===================================================================== */
/* ProcessorSupportsSerialize                                            */
/* ===================================================================== */

/*!
 * Returns non-zero if the processor supports the SERIALIZE instruction
 * (CPUID leaf 7, subleaf 0, EDX bit 14 as defined by Intel SDM vol.2).
 */
static inline int ProcessorSupportsSerialize(void)
{
    int info[4];

#if defined(_MSC_VER)
    __cpuidex(info, 7, 0);
#elif defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("cpuid" : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3]) : "a"(7), "c"(0));
#else
#error "supports_serialize.h: no CPUID implementation for this compiler"
#endif

    return (info[3] >> 14) & 1; /* EDX bit 14 */
}

#endif /* SUPPORTS_SERIALIZE_H */
