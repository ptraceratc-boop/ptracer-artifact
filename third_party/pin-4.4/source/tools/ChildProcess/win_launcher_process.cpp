/*
 * Copyright (C) 2008-2025 Intel Corporation.
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
        std::cout << "App: WaitForSingleObject failed" << std::endl;
        return FALSE;
    }
    DWORD processExitCode;
    if (GetExitCodeProcess(process, &processExitCode) == FALSE)
    {
        std::cout << "App: GetExitCodeProcess Failed" << std::endl;
        return FALSE;
    }
    if (processExitCode != 0)
    {
        std::cout << "App: Got unexpected exit code" << std::endl;
        return FALSE;
    }
    return TRUE;
}

/*
 * Split std::string into 2 sub strings.
 * Example1: prefix = SplitString(input = "aaa bbb ccc", " ") ->
 *           prefix = "aaa", input = "bbb ccc"
 * Example2: prefix = SplitString(input = "aaa", " ") ->
 *           prefix = "aaa", input = ""
 *
 * [inout]  input  - input std::string. remove prefix and set the new std::string into input.
 * [in]     delimiter - delimiter 
 *                
 * return prefix std::string
 */
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

    {
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(STARTUPINFO);
        memset(&pi, 0, sizeof(pi));
        if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            std::cout << "App: Couldn't create child process, command line = " << cmdLine << ", system error =  " << GetLastError() << std::endl;
            exit(-1);
        }
        if (WaitAndVerify(pi.hProcess) == FALSE)
        {
            exit(-1);
        }
        std::cout << "App: First Child Process was created successfully!" << std::endl;
    }

    return 0;
}
