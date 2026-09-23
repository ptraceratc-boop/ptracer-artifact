/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Launch a Windows application and write its PID to stdout.
 * This is used in tests to get the actual Windows process ID.
 * Also, close the application when this launcher process exits.
 */

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <Windows.h>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <application> [args...]" << std::endl;
        return -1;
    }

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

    // Create a job object to ensure child process terminates when this process exits
    HANDLE hJob = CreateJobObject(NULL, NULL);
    if (hJob)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        std::cout << "0" << std::endl;
        std::cerr << "Failed to create " << cmdLine << std::endl;
        if (hJob) CloseHandle(hJob);
        return -1;
    }

    // Add the process to the job object
    if (hJob)
    {
        AssignProcessToJobObject(hJob, pi.hProcess);
    }

    // Resume the process
    ResumeThread(pi.hThread);

    // Output the Windows PID
    char digitBuffer[64];
    std::cout << itoa(pi.dwProcessId, digitBuffer, 10) << std::endl;
    std::cout.flush();

    // Wait for the child process to complete
    // This keeps win_app_launcher alive, maintaining the job object
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Close handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hJob) CloseHandle(hJob);

    return 0;
}
