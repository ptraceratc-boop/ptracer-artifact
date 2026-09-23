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

int main()
{
    char cmd1[] = "win_child_process.exe \"param1 param2\" param3"; //"dir create*";
    STARTUPINFO startupInfo;
    PROCESS_INFORMATION processInfo;
    memset(&startupInfo, 0, sizeof(startupInfo));
    startupInfo.cb = sizeof(STARTUPINFO);
    memset(&processInfo, 0, sizeof(processInfo));

    if (!CreateProcess(NULL, cmd1, NULL, NULL, TRUE, NULL, NULL, NULL, &startupInfo, &processInfo))
    {
        std::cout << "CreateProcess failed for " << std::string(cmd1) << std::endl;
        exit(0);
    }
    if (WaitAndVerify(processInfo.hProcess) == FALSE)
    {
        exit(1);
    }
    return 0;
}
