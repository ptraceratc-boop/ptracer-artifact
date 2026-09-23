/*
 * Copyright (C) 2021-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <iomanip>
#include <windows.h>

/*
 * This application tests memory allocation and protection via broker.
 * It calls SetProcessMitigationPolicy, which restricts allocating pages with executable permissions, or
 * granting executable permissions to an existing page.
 * If this application finishes, it means Pin successfully allocated memory AND changed protection via broker.
 */
int main()
{
    const int sleepMiliSeconds = 10000;

    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY processMitigationObject;
    processMitigationObject.Flags               = 0x0;
    processMitigationObject.ProhibitDynamicCode = 0x1;
    void* lpBuffer;
    size_t dwLength = sizeof(processMitigationObject);

    lpBuffer = static_cast< void* >(&processMitigationObject);
    bool res = SetProcessMitigationPolicy(ProcessDynamicCodePolicy, lpBuffer, dwLength);
    if (!res)
    {
        std::cerr << "SetProcessMitigationPolicy failed with 0x" << std::hex << GetLastError() << " exit status." << std::endl;
        exit(-2);
    }
    std::cout << "Going to sleep for " << sleepMiliSeconds << " miliseconds." << std::endl;
    Sleep(sleepMiliSeconds);

    return 0;
}
