/*
 * Copyright (C) 2018-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* This test tests three things:

-First reserves memory using the knob and verifies that all requested pages are reserved.
If it is unsuccessful the test should return 1 or 3.

-Second makes sure every page within the ranges provided is reserved, returns 3 if not.

-Third tests scenarios where error messages should be returned. The makefile runs separate inputs for merge success,
collision failure, malformed input, and illegal reversed ranges.

In winrange.address we ask to reserve the following addresses, and expect to see the log messages as stated:

0x40010000:0x40020000
0x40011000:0x40021000
0x40020000:0x40030000
0x40030000:0x40040000
These ranges overlap or touch after alignment, so the injector should merge them and reserve the whole span.
This address is not aligned to the 64K we expect. Will reserve after fixing alignment, expect to see:
RESERVE MEMORY: Base Address misaligned, rounding down to: 0x40010000
RESERVE MEMORY: End Address misaligned, rounding up to: 0x40030000

0x40010000:0x40000000
Here begin>end expect:
ERROR: RESERVE MEMORY: Illegal range  0x040010000:0x040000000
*/
#include <stdio.h>
#include <assert.h>
#include <windows.h>

int main(int argc, char* argv[])
{
    FILE* f         = fopen(argv[1], "r");
    int access_flag = atoi(argv[2]);
    if (!f)
    {
        fprintf(stderr, "cannot open file %s\n", argv[1]);
        return 2;
    }

    uintptr_t low = 0, high = 0;
    int tid;
    char desc[64];

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int page_size    = si.dwPageSize;
    bool knobSuccess = false;
    bool allReserved = true;
    while (!feof(f))
    {
        fscanf(f, "%Ix %Ix %s %d", &low, &high, desc, &tid);

        if (high < low)
        {
            continue;
        }

        for (uintptr_t i = low; i < high; i += page_size)
        {
            MEMORY_BASIC_INFORMATION reg;
            VirtualQuery((void*)(i), &reg, sizeof(MEMORY_BASIC_INFORMATION));
            if (access_flag == 1)
            {
                if (reg.State == MEM_COMMIT && reg.Protect == PAGE_EXECUTE_READWRITE)
                { // Reserve_mem commits free ranges with application-accessible protection.
                    knobSuccess = true;
                }
                else
                {
                    allReserved = false;
                }
            }
        }
    }

    fclose(f);

    if (knobSuccess == false && access_flag == 1)
    {
        return 1;
    }
    if (allReserved == false && access_flag == 1)
    {
        return 3;
    }

    return 0;
}