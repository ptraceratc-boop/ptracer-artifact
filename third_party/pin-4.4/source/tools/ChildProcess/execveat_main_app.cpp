/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This test checks the execveat function. Depending on the input parameter, it will check positive or negative flows.
 * In addition to what this test checks, we also check that an environment variable which is set after Pin took over the
 * application is being passed to the application which will be executed by the current application.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <gnu/libc-version.h>

#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34)
// execveat is available
#else
// fallback definition of execveat for older glibc versions, however probe mode will not work correctly without proper glibc support
#include <syscall.h>
extern char** environ;
int execveat(int dirfd, const char* pathname, char* const argv[], char* const envp[] = environ, int flags = 0)
{
    printf("Warning: using fallback execveat definition, this is ok for jit mode, however probe mode will not work correctly "
           "without proper glibc support\n");
    return syscall(__NR_execveat, dirfd, pathname, argv, envp, flags);
}
#endif

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Specify child application name and test number (1-15): %s <child app name> <test number>\n", argv[0]);
        exit(-1);
    }

    // Checking if an environment variable which is set after Pin took over the application is being passed to
    // the application we're about to execute below (as expected)
    setenv("ParentEnv", "1", 1);
    assert(strcmp(getenv("ParentEnv"), "1") == 0);

    printf("parent ppid/pid: %u/%u\n", getppid(), getpid());

    int fd;
    int ret;
    unsigned int t = (unsigned int)time(NULL);
    char symlinkPath[512];
    char fullAppPath[4096];

    snprintf(symlinkPath, sizeof(symlinkPath), "/tmp/follow_child2_symlink_%u", t);

    // select which test to run
    switch (atoi(argv[2]))
    {
        case 1:
            fd = open(argv[1], O_PATH | O_CLOEXEC);
            if (execveat(fd, "", NULL, environ, AT_EMPTY_PATH) == -1)
            {
                fprintf(stderr, "execveat failed: errno %d (%s)\n", errno, strerror(errno));
            }
            break;
        case 2:
            if (execveat(AT_FDCWD, argv[1], NULL, environ, 0) == -1)
            {
                fprintf(stderr, "execveat failed: errno %d (%s)\n", errno, strerror(errno));
            }
            break;
        case 3:
        {
            char buffer[4096];
            if (getcwd(buffer, sizeof(buffer)) == NULL)
            {
                fprintf(stderr, "getcwd failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            // find last '/' in the path and terminate the string there to get a path to one directory up
            char* lastSlash = strrchr(buffer, '/');
            *lastSlash      = '\0';
            fd              = open(buffer, O_PATH | O_CLOEXEC);
            // now add the application name to the remaining one directory
            char* relativePath = lastSlash + 1;
            strcat(relativePath, "/");
            strcat(relativePath, argv[1]);

            if (execveat(fd, relativePath, NULL, environ, 0) == -1)
            {
                fprintf(stderr, "execveat failed: errno %d (%s)\n", errno, strerror(errno));
            }
            break;
        }
        case 4:
            // create symbolic link to the application and try to execute it via execveat
            // remove existing symlink if any
            unlink(symlinkPath);
            // get full path of the application
            if (realpath(argv[1], fullAppPath) == NULL)
            {
                fprintf(stderr, "realpath failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            // create symlink
            if (symlink(fullAppPath, symlinkPath) == -1)
            {
                fprintf(stderr, "symlink creation failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            fd = open("/tmp", O_PATH | O_CLOEXEC);
            if (execveat(fd, &symlinkPath[5], NULL, environ, 0) == -1)
            {
                fprintf(stderr, "execveat failed: errno %d (%s)\n", errno, strerror(errno));
            }
            break;
        case 5:
            // create symbolic link to the application and try to execute it via execveat with fd and AT_SYMLINK_NOFOLLOW
            // remove existing symlink if any
            unlink(symlinkPath);
            // get full path of the application
            if (realpath(argv[1], fullAppPath) == NULL)
            {
                fprintf(stderr, "realpath failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            // create symlink
            if (symlink(fullAppPath, symlinkPath) == -1)
            {
                fprintf(stderr, "symlink creation failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            fd = open("/tmp", O_PATH | O_CLOEXEC);
            if (execveat(fd, &symlinkPath[5], NULL, environ, AT_SYMLINK_NOFOLLOW) == -1)
            {
                if (errno != ELOOP)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be ELOOP)\n", errno, strerror(errno));
                    unlink(symlinkPath);
                    return 1;
                }
                return 0;
            }
            break;
        case 6:
            // create symbolic link to the application and try to execute it via execveat with AT_SYMLINK_NOFOLLOW
            // remove existing symlink if any
            unlink(symlinkPath);
            // get full path of the application
            if (realpath(argv[1], fullAppPath) == NULL)
            {
                fprintf(stderr, "realpath failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            // create symlink
            if (symlink(fullAppPath, symlinkPath) == -1)
            {
                fprintf(stderr, "symlink creation failed: errno %d (%s)\n", errno, strerror(errno));
                break;
            }
            if (execveat(-1, symlinkPath, NULL, environ, AT_SYMLINK_NOFOLLOW) == -1)
            {
                if (errno != ELOOP)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be ELOOP)\n", errno, strerror(errno));
                    unlink(symlinkPath);
                    return 1;
                }
                return 0;
            }
            break;
        case 7:
            fd = open("/tmp", O_PATH | O_CLOEXEC);
            if (execveat(fd, "non_existing_file", NULL, environ, 0) == -1)
            {
                if (errno != ENOENT)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be ENOENT)\n", errno, strerror(errno));
                    unlink(symlinkPath);
                    return 1;
                }
                return 0;
            }
            break;
        case 8:
            // check invalid flag
            if (execveat(-1, argv[1], NULL, environ, AT_SYMLINK_FOLLOW) == -1)
            {
                if (errno != EINVAL)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EINVAL)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 9:
            // test illegal use of NULL filename with AT_EMPTY_PATH, this also fails with direct os call (no pin)
            fd = open(argv[1], O_PATH | O_CLOEXEC);
            if (execveat(fd, NULL, NULL, environ, AT_EMPTY_PATH) == -1)
            {
                if (errno != EFAULT)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EFAULT)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 10:
            // test illegal use of NULL filename with AT_EMPTY_PATH, this also fails with direct os call (no pin)
            if (execveat(-1, NULL, NULL, environ, AT_EMPTY_PATH) == -1)
            {
                if (errno != EFAULT)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EFAULT)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 11:
            // test illegal use of NULL filename, this also fails with direct os call (no pin)
            fd = open(argv[1], O_PATH | O_CLOEXEC);
            if (execveat(fd, NULL, NULL, environ, 0) == -1)
            {
                if (errno != EFAULT)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EFAULT)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 12:
            // test illegal use of NULL filename, this also fails with direct os call (no pin)
            if (execveat(-1, NULL, NULL, environ, 0) == -1)
            {
                if (errno != EFAULT)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EFAULT)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 13:
            // test bad file descriptor with NULL filename and AT_EMPTY_PATH, this also fails with direct os call (no pin)
            if (execveat(1111, "", NULL, environ, AT_EMPTY_PATH) == -1)
            {
                if (errno != EBADF)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EBADF)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 14:
            // test bad file descriptor with the real filename, this also fails with direct os call (no pin)
            if (execveat(1111, argv[1], NULL, environ, 0) == -1)
            {
                if (errno != EBADF)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be EBADF)\n", errno, strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        case 15:
            // test a file's file descriptor (and not a directory) with a relative filename
            fd = open(argv[1], O_PATH | O_CLOEXEC);
            if (execveat(fd, argv[1], NULL, environ, 0) == -1)
            {
                if (errno != ENOTDIR)
                {
                    fprintf(stderr, "execveat failed but wrong errno: errno %d (%s, should be ENOTDIR)\n", errno,
                            strerror(errno));
                    return 1;
                }
                return 0;
            }
            break;
        default:
            fprintf(stderr, "Invalid test number: %s\n", argv[2]);
            return 1;
    }

    fprintf(stderr, "Unknown error occurred, the test should not get here!\n");
    return 1;
}
