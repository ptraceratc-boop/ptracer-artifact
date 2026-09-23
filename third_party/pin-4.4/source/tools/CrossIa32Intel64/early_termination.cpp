/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <Windows.h>
#include <stdio.h>
#include <iostream>
#include <string>

extern "C" __declspec(dllimport) int Something();




//this application has static linking with dll which terminate the process in it's DllMain(PROCESS_ATTACH)
int main()
{
    //should never get here
    std::cout << "should never get here" << std::endl << std::flush;
    volatile int i = Something();
    exit(-1);
    return 0;
}
