/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This test checks the execveat function. This is the application which is being execed by the main application.
 * In addition to what this test checks, we also check in here that an environment variable which was set after Pin
 * took over the main application was passed properly to this application.
 */
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string>

int main(int argc, char** argv, char* envp[])
{
    printf("child ppid/pid: %u/%u\n", getppid(), getpid());

    int i = 0;

    // Verifying that 'ParentEnv' environment variable which was set by the application (after Pin tool over)
    // which executed the current application was passed as expected
    bool parentEnvPassed = false;
    while (envp[i] != NULL)
    {
        if (std::string(envp[i]).compare(std::string("ParentEnv=1")) == 0)
        {
            parentEnvPassed = true;
            break;
        }
        i++;
    }

    assert(parentEnvPassed);

    return 0;
}
