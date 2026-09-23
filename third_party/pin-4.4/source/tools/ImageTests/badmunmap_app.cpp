/*
 * Copyright (C) 2013-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <stdio.h>





#if defined(TARGET_WINDOWS)
#include <windows.h>
#define EXPORT_SYM extern "C" __declspec(dllexport) __declspec(noinline)
#else
#include <sys/mman.h>
#define EXPORT_SYM extern "C"
#endif

const unsigned int arraySize = 1000;
static char theArray[arraySize];

EXPORT_SYM void AppMarker() { std::cout << "APP: AppMarker executed" << std::endl << std::flush; }

int main()
{
    std::cout << "APP: Begin test" << std::endl << std::flush;
    std::cout << "APP: calling munmap(" << std::hex << (void*)theArray << "," << std::dec << arraySize << ")" << std::endl << std::flush;
#if defined(TARGET_WINDOWS)
    if (0 != VirtualFree(theArray, arraySize, MEM_RELEASE))
    {
        std::cout << "APP: ERROR: munmap of a variable in the BSS (wrongly) succeeded";
        return 1;
    }
#else
    if (0 == munmap(theArray, arraySize))
    {
        std::cout << "APP: ERROR: munmap of a variable in the BSS (wrongly) succeeded";
        return 1;
    }
#endif
    perror("APP: munmap failed as expected");
    AppMarker();
    std::cout << "APP: End test" << std::endl << std::flush;
    return 0;
}
