/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <Windows.h>
#include <iostream>
#include <string>





// launch given application, write application's PID to stdout

int main(int argc, char* argv[], char* envp[])
{
    std::string cmdLine = "";
    // Build command line
    for (int i = 1; i < argc; i++)
    {
        cmdLine += std::string(argv[i]);
        if (i != argc - 1)
        {
            cmdLine += std::string(" ");
        }
    }

    // Invoke Application
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        std::cout << "0" << std::endl;
        std::cerr << "Failed to create " << cmdLine << std::endl;
        exit(-1);
    }

    char digitBuffer[64];
    std::cout << itoa(pi.dwProcessId, digitBuffer, 10);
    return 0;
}
