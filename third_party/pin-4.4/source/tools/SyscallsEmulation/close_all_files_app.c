/*
 * Copyright (C) 2016-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
* This file together with tool and makefile checks that Pin guards the file descriptors opened by itself and the
* tool (including pin.log), and doesn't let the application to close them.
*
* It also checks that when the application is closing STD file descriptors (STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO):
* 1. The next print in app will not be printed to console.
* 2. Prints by Pin/Tool to STD file descriptors are still possible. 
*    The test checks that prints by the tool to STDERR are printed.
*
* TODO check when the tool is closing one of the STD's it doesn't affect the application. 
* For example if the tool close STDOUT_FILENO the application can still write to it.
*/
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#if defined(TARGET_LINUX)
const char* DIR_FD = "/proc/self/fd";
#endif

void closeAllFilesExceptStd()
{
    DIR* d = opendir(DIR_FD);
    assert(NULL != d);
    struct dirent* ent;
    while (NULL != (ent = readdir(d)))
    {
        if (ent->d_name[0] == '.')
        {
            continue;
        }
        char* endptr;
        errno             = 0;
        const long int fd = strtol(ent->d_name, &endptr, 10);
        if (*endptr || fd < 0 || errno)
        {
            continue;
        }
        if (fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO)
        {
            continue;
        }
        int ret;
        do
        {
            ret = close(fd);
        }
        while (ret == -1 && errno == EINTR);
        assert(0 == ret);
    }
}

void closeStdFDs()
{
    int ret, i = 0;
    // close STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO
    for (i; i < 3; ++i)
    {
        ret = close(i);
        assert(0 == ret);
    }
}

int main()
{
    closeAllFilesExceptStd();
    // Should print to console.
    printf("Application after closeAllFilesExceptStd()\n");
    fflush(stdout);
    closeStdFDs();
    // Should NOT print to console.
    printf("Application after closeStdFDs() should not reach console\n");
    fflush(stdout);
    int pid = fork();
    if (pid != 0)
    {
        // This is just for safety, letting forked child finish before checking stuff in makefile
        sleep(1);
    }
    return 0;
}
