/*
 * Copyright (C) 2017-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Application launcher used for testing Pin in attach mode (as opposed to launch mode)
 * It creates a child process (which is the application we want Pin to attach to) and write its process ID
 * to the output which is later used by Pin to attach to it.
 * The difference between this launcher and w_app_launcher.cpp is that this launcher waits for child process to
 * notify it is ready to be attached by Pin (using semaphore). Intended for tests that the attach point the
 * in time is important for them.
 */
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <assert.h>
#include <Windows.h>
#include <iostream>
#include <sstream>
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

    // Create child process suspended in order to get its process ID first (see below why)
    //
    if (!CreateProcess(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        std::cout << "0" << std::endl;
        std::cerr << "Failed to create " << cmdLine << std::endl;
        exit(-1);
    }

    //
    // Wait for child process to notify it is ready to be attached by Pin by using the below semaphore.
    //

    // Create a unique semaphore handle name using the child process ID
    std::ostringstream stream;
    stream << pi.dwProcessId;
    std::string semaphoreHandleName = "semaphore_handle_" + stream.str();

    HANDLE ChildReadySemaphore;
    ChildReadySemaphore = CreateSemaphore(NULL,                         // default security attributes
                                          0,                            // initial count
                                          1,                            // maximum count
                                          semaphoreHandleName.c_str()); // unnamed semaphore

    // Now that the semaphore handle exist we can resume the child process which will
    // use this handle (by using the unique name)
    ResumeThread(pi.hThread);

    std::cerr << "  Launcher: waiting on " << semaphoreHandleName << std::endl;
    DWORD waitResult = WaitForSingleObject(ChildReadySemaphore, // handle to semaphore
                                           30000);              // wait 30 seconds

    if (waitResult == WAIT_TIMEOUT)
    {
        std::cerr << "Error: Child process didn't notify it is ready to be attached by Pin for more than 30 seconds. ";
        return 1;
    }
    assert(waitResult == WAIT_OBJECT_0);

    std::cerr << "  Launcher: " << semaphoreHandleName << " was relesed by App" << std::endl;

    // Now we can proceed to attach Pin to child process by writing its process ID (will be used by Pin)
    char digitBuffer[64];
    std::cout << itoa(pi.dwProcessId, digitBuffer, 10);

    CloseHandle(ChildReadySemaphore);

    return 0;
}
