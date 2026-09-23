/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>

#if defined(_WIN32)
#define EXPORT_CSYM extern "C" __declspec(dllexport)
#define NOINLINE __declspec(noinline)
#else
#define EXPORT_CSYM extern "C"
#define NOINLINE __attribute__((noinline))
#endif

EXPORT_CSYM NOINLINE int IargReturnRegsProbeTarget()
{
    static volatile int value = 7;
    value += 3;
    value -= 3;
    return value;
}

int main()
{
    const int result = IargReturnRegsProbeTarget();
    if (result != 7)
    {
        std::cerr << "Unexpected target result: " << result << std::endl;
        return 1;
    }

    std::cout << "Test finished successfully" << std::endl;
    return 0;
}