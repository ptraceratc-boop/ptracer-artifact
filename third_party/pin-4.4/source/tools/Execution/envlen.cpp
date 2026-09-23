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

#ifdef _WIN32
extern char** _environ;
#else
extern char** environ;
#endif

int main(int argc, char** argv)
{
    // Check if we should re-launch via execve
    if (argc > 1 && strcmp(argv[1], "execve") == 0)
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
        // The environment block is passed in the PEB of the new process, so we don't need to do anything special to pass it.
        // If we want to pass the environment block explicitly, we would need to use CreateProcessW since CreateProcessA is limited to 32K.
        if (!CreateProcessA(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            fprintf(stderr, "envlen: CreateProcess failed, error=%lu\n", GetLastError());
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

    size_t totalLen    = 0;
    int envCount       = 0;
    const char* prefix = "TEST_ENV_VAR";
    size_t prefixLen   = strlen(prefix);

    // Calculate total length of environment variables that start with TEST_ENV_VAR
#ifdef _WIN32
    char** env = _environ;
#else
    char** env = environ;
#endif

    for (; *env != nullptr; env++)
    {
        // Check if this environment variable starts with the prefix
        if (strncmp(*env, prefix, prefixLen) == 0)
        {
            totalLen += strlen(*env) + 1; // +1 for the null terminator
            envCount++;
        }
    }

    // Print both environment variable count and total length
    printf("envcount = %d, total_len = %zu\n", envCount, totalLen);

    return 0;
}
