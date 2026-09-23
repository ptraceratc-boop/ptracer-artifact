/*
 * Copyright (C) 2016-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <iostream>




extern "C" __declspec(noinline, dllexport) int PinIsAttached() { return 0; }

extern "C" __declspec(noinline, dllexport) int PinIsDetached() { return 1; }

extern "C" __declspec(noinline, dllexport) void FirstProbeInvoked()
{
    std::cerr << "FirstProbeInvoked shouldn't be called" << std::endl;
    abort();
}

extern "C" __declspec(noinline, dllexport) void SecondProbeInvoked()
{
    std::cerr << "SecondProbeInvoked shouldn't be called" << std::endl;
    abort();
}

int main()
{
    while (!PinIsAttached())
        SwitchToThread();
    FirstProbeInvoked();
    while (!PinIsDetached())
        SwitchToThread();
    while (!PinIsAttached())
        SwitchToThread();
    SecondProbeInvoked();
    while (!PinIsDetached())
        SwitchToThread();
    while (!PinIsAttached())
        SwitchToThread();

    std::cerr << "Test passed!" << std::endl;

    return 0;
}
