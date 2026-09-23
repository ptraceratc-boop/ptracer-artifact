/*
 * Copyright (C) 2019-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <string>
#include <iostream>
#include <errno.h>
#include <windows.h>




/**************************************************/

extern "C" __declspec(dllexport) __declspec(noinline) int AfterSetCurrentDirectory()
{
    // Pin replaces this function to validate loading of a Pin DLL.
    std::cout << "Should not run" << std::endl;
    return 3; // Returns failure 3 if not instrumented.
}

int main()
{
    // Modify current working directory of the process.
    if (!SetCurrentDirectory("..\\")) return 4; // Return on failure.

    char buf[MAX_PATH * 10];
    DWORD nsize = GetCurrentDirectory(sizeof(buf), buf);
    if ((nsize == 0) || (nsize > sizeof(buf))) return 5; // Can't get current directory name.
    std::cout << "Current directory: " << buf << std::endl;

    int res = AfterSetCurrentDirectory();
    return res;
}
