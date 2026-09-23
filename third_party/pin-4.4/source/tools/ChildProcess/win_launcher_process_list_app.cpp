/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// This application receives a text file as input.
// Each line decribes a command line.
// This process reads those lines one by one and creates the corresponding process

#include <Windows.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

//Wait for a process completion
//Verify it returned the expected exit code
bool WaitAndVerify(HANDLE process, char **argv)
{
    if (WaitForSingleObject(process, INFINITE) == WAIT_FAILED)
    {
        std::cout << "WaitForSingleObject failed" << std::endl;
        return FALSE;
    }
    DWORD processExitCode;
    if (GetExitCodeProcess(process, &processExitCode) == FALSE)
    {
        std::cout << argv[0] << ": GetExitCodeProcess Failed" << std::endl;
        return FALSE;
    }
    if (processExitCode != 0)
    {
        std::cout << argv[0] << ": Got unexpected exit code" << std::endl;
        return FALSE;
    }
    return TRUE;
}

int create_process(std::string cmdLine, std::string prefixDir, char** argv)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    
    char currDir[4096];
    
    DWORD ret = GetCurrentDirectory(sizeof(currDir), currDir);
    if (ret == 0 || ret >= sizeof(currDir))
    {
        std::cout << "Can't get current directory" << std::endl;
        return -1;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));
    
    std::string fullPathCmd = currDir;

    fullPathCmd += "\\";
    fullPathCmd += prefixDir;
    fullPathCmd += "\\";
    
    fullPathCmd += cmdLine;
    
    if (!CreateProcess(NULL, (LPSTR)fullPathCmd.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        std::cout << "Couldn't create child process, command line = " << fullPathCmd << ", system error =  " << GetLastError() << std::endl;
        return -1;
    }
    if (WaitAndVerify(pi.hProcess, argv) == FALSE)
    {
        return -1;
    }

    return 0;
}

int main(int argc, char* argv[])
{
    assert(3 == argc);
    
    std::string prefDir = argv[1];
    std::ifstream file(argv[2]);
    if (!file.is_open())
    {
        std::cout << "Can't open file " << argv[2] << std::endl;
        exit(-1);
    }

    std::string line;
    while (getline(file, line))
    {
        if ('#' == line[0])
            continue;
        std::cerr << "Grand parent: Processing: " << line << std::endl;
        if (create_process(line, prefDir, argv))
        {
            exit(-1);
        }

        std::cerr << "Grand parent: child Process was created successfully!\n";
    }

    file.close();
    return 0;
}
