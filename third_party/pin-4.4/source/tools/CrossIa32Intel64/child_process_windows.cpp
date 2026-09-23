/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// Application that creates new process

#include <Windows.h>
#include <iostream>
#include <string>





//Wait for a process completion
//Verify it returned the expected exit code
bool WaitAndVerify(HANDLE process)
{
    if (WaitForSingleObject(process, INFINITE) == WAIT_FAILED)
    {
        std::cout << "WaitForSingleObject failed" << std::endl;
        return FALSE;
    }
    DWORD processExitCode;
    if (GetExitCodeProcess(process, &processExitCode) == FALSE)
    {
        std::cout << "GetExitCodeProcess Failed" << std::endl;
        return FALSE;
    }
    if (processExitCode != 0)
    {
        std::cout << "Got unexpected exit code" << std::endl;
        return FALSE;
    }
    return TRUE;
}

std::string SplitString(std::string* input, const std::string& delimiter = " ")
{
    std::string::size_type pos = input->find(delimiter);
    std::string substr         = input->substr(0, pos);
    if (pos != std::string::npos)
    {
        *input = input->substr(pos + 1);
    }
    else
    {
        *input = "";
    }
    return substr;
}

int main(int argc, char* argv[])
{
    std::string cmdLine = GetCommandLine();
    SplitString(&cmdLine);
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, NULL, NULL, NULL, &si, &pi))
    {
        std::cout << "Couldn't create grand child process " << std::endl;
        exit(0);
    }
    if (WaitAndVerify(pi.hProcess) == FALSE)
    {
        exit(0);
    }
    std::cout << " Grand Child Process was created successfully!" << std::endl;

    return 0;
}
