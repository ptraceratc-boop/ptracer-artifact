/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <Windows.h>
#include <stdio.h>
#include <iostream>
#include <string>



extern "C" __declspec(dllimport) __declspec(noinline) int Something();

//this application has static linking with dll which creates thread in it's DllMain(PROCESS_ATTACH)
int main()
{
    std::cout << "in main()" << std::endl << std::flush;
    volatile int i = Something();
    return 0;
}
