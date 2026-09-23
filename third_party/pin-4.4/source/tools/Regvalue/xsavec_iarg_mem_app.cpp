/*
 * Copyright (C) 2015-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include <cstdio>

#ifdef TARGET_WINDOWS
#define ASMNAME(name)
#define ALIGN64 __declspec(align(64))
#else
#define ASMNAME(name) asm(name)
#define ALIGN64 __attribute__((aligned(64)))
#endif

#ifdef TARGET_IA32
typedef long ADDRINT;
#define ADDRINT_FORMAT "%lx"
#else
typedef long long ADDRINT;
#define ADDRINT_FORMAT "%llx"
#endif

/////////////////////
// EXTERNAL FUNCTIONS
/////////////////////

extern "C" void DoXsavec();
extern "C" void DoXsaveOpt();
extern "C" void DoXrstor();

/////////////////////
// GLOBAL VARIABLES
/////////////////////

extern "C"
{
    // the current size is large enough for avx512
    unsigned char ALIGN64 xsaveArea[2688] ASMNAME("xsaveArea");
    ADDRINT flags ASMNAME("flags");
}

ADDRINT checkedFlags[8] = {
    0x00, /* none */
    0x03, /* only legacy */
    0x04, /* only avx    */
    0x07, /* combo1 legacy + AVX */
    0x20, /* only OPMASK */
    0xe0, /* AVX512 (OPMASK + ZMM_H + ZMM) */
    0xc4, /* combo2 AVX + ZMM_H + ZMM */
    0xe7  /* all non-BND (legacy + AVX + AVX512) */
};

int main(int argc, const char* argv[])
{
    memset(xsaveArea, 0, sizeof(xsaveArea));

    for (int i = 0; i < 8; i++)
    {
        flags = checkedFlags[i];

        DoXsavec(); // get the register value before the change
        printf("XSAVE on 0x" ADDRINT_FORMAT "\n", (ADDRINT)xsaveArea);

        DoXrstor(); // restor the register value
        printf("XRSTOR on 0x" ADDRINT_FORMAT "\n", (ADDRINT)xsaveArea);
    }

    return 0;
}
