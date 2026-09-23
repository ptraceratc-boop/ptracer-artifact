/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// Cross-platform application for get_pin_cmd_line test.
// Dual-mode:
//   argv[1] == "1": Parent mode - creates a child process with argv[1] set to "0"
//   argv[1] == "0": Child mode - prints success and exits

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <1|0>" << std::endl;
        return 1;
    }

    int mode = atoi(argv[1]);

    if (mode == 0)
    {
        // Child mode
        std::cout << "Child process ran successfully" << std::endl;
        return 0;
    }

    // Parent mode: create child process with argv[1] set to "0"
#ifdef _WIN32
    std::string cmdLine    = std::string(argv[0]) + " 0";
    STARTUPINFO si         = {};
    si.cb                  = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        std::cerr << "CreateProcess failed, error = " << GetLastError() << std::endl;
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        std::cerr << "GetExitCodeProcess failed, error = " << GetLastError() << std::endl;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0)
    {
        std::cerr << "Child process failed with exit code " << exitCode << std::endl;
        return 1;
    }
#else
    pid_t pid = fork();
    if (pid == 0)
    {
        char* childArgv[] = {argv[0], (char*)"0", NULL};
        execv(argv[0], childArgv);
        // shouldn't return unless execv failed
        std::cerr << "execv failed" << std::endl;
        return 1;
    }
    else if (pid < 0)
    {
        std::cerr << "fork failed" << std::endl;
        return 1;
    }

    // else - fork succeeded, in parent process, pid is child PID
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        std::cerr << "Child process failed" << std::endl;
        return 1;
    }
#endif

    return 0;
}
