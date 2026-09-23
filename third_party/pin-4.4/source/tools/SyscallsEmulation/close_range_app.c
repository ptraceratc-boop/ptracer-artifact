/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */


/*
 * Opens application-owned file descriptors and verifies that close_range()
 * closes them while preserving standard file descriptors.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NUMBER_OF_FILES 100

int main()
{
    int fds[NUMBER_OF_FILES];
    int highestFd = STDERR_FILENO;

    for (int i = 0; i < NUMBER_OF_FILES; ++i)
    {
        fds[i] = open("/dev/null", O_RDONLY);
        if (-1 == fds[i])
        {
            perror("open");
            return 1;
        }

        if (fds[i] > highestFd)
        {
            highestFd = fds[i];
        }
    }

    // Verify errno is set correctly when close_range() is called with invalid arguments.
    // start > end is an invalid range - we expect close_range() to fail and set errno to EINVAL.
    if (-1 != close_range(highestFd, STDERR_FILENO + 1, 0))
    {
        fprintf(stderr, "close_range did not fail with invalid arguments\n");
        return 1;
    }
    if (EINVAL != errno)
    {
        fprintf(stderr, "close_range did not set errno to EINVAL for invalid arguments\n");
        return 1;
    }

    // The range is sure to include Pin-owned file descriptors when running under Pin.
    // The syscall is expected to close all application-owned file descriptors in the range,
    // but not Pin-owned file descriptors.
    if (-1 == close_range(STDERR_FILENO + 1, highestFd, 0))
    {
        perror("close_range");
        return 1;
    }

    for (int i = 0; i < NUMBER_OF_FILES; ++i)
    {
        errno = 0;
        if ((-1 != fcntl(fds[i], F_GETFD)) || (EBADF != errno))
        {
            fprintf(stderr, "Application file descriptor %d was not closed\n", fds[i]);
            return 1;
        }
    }

    printf("Application close_range succeeded\n");
    return 0;
}