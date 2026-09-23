/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <cstring>
#include <cstdio>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

int main(int argc, char** argv)
{
    // Check if the last argument is "execve"
    if (argc > 1 && strcmp(argv[argc - 1], "execve") == 0)
    {
#ifdef _WIN32
        // Build a quoted command line, skipping the last "execve" argument
        std::string cmdLine;
        for (int i = 0; i < argc - 1; i++)
        {
            if (i > 0) cmdLine += ' ';
            cmdLine += '"';
            for (const char* p = argv[i]; *p; ++p)
            {
                if (*p == '"') cmdLine += '\\';
                cmdLine += *p;
            }
            cmdLine += '"';
        }

        STARTUPINFOA si        = {};
        si.cb                  = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessA(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            fprintf(stderr, "arglen: CreateProcess failed, error=%lu\n", GetLastError());
            return 1;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return (int)exitCode;
#else
        // Re-launch without the "execve" argument
        char** newArgv = new char*[static_cast< size_t >(argc)];
        for (int i = 0; i < argc - 1; i++)
        {
            newArgv[i] = argv[i];
        }
        newArgv[argc - 1] = nullptr;

        execve(argv[0], newArgv, environ);

        // If execve fails, print error and exit
        perror("execve failed");
        delete[] newArgv;
        return 1;
#endif
    }

    size_t totalLen = 0;

    // Calculate total length of all arguments
    for (int i = 1; i < argc; i++)
    {
        if (argv[i])
        {
            totalLen += strlen(argv[i]) + 1; // +1 for the null terminator
        }
    }

    // Print both argument count and total length
    printf("argc = %d, total_len = %zu\n", argc, totalLen);

    return 0;
}
