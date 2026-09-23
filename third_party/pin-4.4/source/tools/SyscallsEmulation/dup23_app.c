/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Duplicates an application descriptor over the native descriptor underlying
 * a Pin-owned generic descriptor. The Pintool replaces
 * GetPinNativeFileDescriptor() with that native descriptor.
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NOINLINE __attribute__((noinline))

int mydup2(int oldfd, int newfd)
{
#if USE_SYSCALLS
    return syscall(__NR_dup2, oldfd, newfd);
#else
    return dup2(oldfd, newfd);
#endif
}

int mydup3(int oldfd, int newfd, int flags)
{
#if USE_SYSCALLS
    return syscall(__NR_dup3, oldfd, newfd, flags);
#else
    return dup3(oldfd, newfd, flags);
#endif
}

// The Pintool replaces this function. Without the Pintool, it returns a file descriptor that is free in this process.
NOINLINE int GetPinNativeFileDescriptor(void)
{
    int freeFd = fcntl(STDERR_FILENO, F_DUPFD, STDERR_FILENO + 1);
    if (-1 == freeFd)
    {
        return -1;
    }

    if (-1 == close(freeFd))
    {
        return -1;
    }

    return freeFd;
}

static int VerifyExpectedErrno(long result, int expectedErrno, const char* description)
{
    if ((-1 != result) || (expectedErrno != errno))
    {
        fprintf(stderr, "%s did not fail with %s\n", description, strerror(expectedErrno));
        return -1;
    }

    return 0;
}

static int VerifyInvalidDupArguments(const char* duplicateSyscall, int applicationFd, int pinNativeFd)
{
    if (0 == strcmp(duplicateSyscall, "dup2"))
    {
        errno = 0;
        return VerifyExpectedErrno(mydup2(applicationFd, -1), EBADF, "dup2 with an invalid target");
    }

    errno = 0;
    if (0 != VerifyExpectedErrno(mydup3(applicationFd, applicationFd, 0), EINVAL, "dup3 with equal file descriptors"))
    {
        return -1;
    }

    errno = 0;
    return VerifyExpectedErrno(mydup3(applicationFd, pinNativeFd, O_NONBLOCK), EINVAL, "dup3 with invalid flags");
}

static int RunDuplicateSyscall(const char* duplicateSyscall)
{
    int applicationFd = open("/dev/null", O_WRONLY);
    if (-1 == applicationFd)
    {
        perror("open");
        return -1;
    }

    int pinNativeFd = GetPinNativeFileDescriptor();
    if (-1 == pinNativeFd)
    {
        perror("GetPinNativeFileDescriptor");
        (void)close(applicationFd);
        return -1;
    }

    if (0 != VerifyInvalidDupArguments(duplicateSyscall, applicationFd, pinNativeFd))
    {
        (void)close(applicationFd);
        return -1;
    }

    long duplicateFd;
    if ('2' == duplicateSyscall[3] && '\0' == duplicateSyscall[4])
    {
        duplicateFd = mydup2(applicationFd, pinNativeFd);
    }
    else if ('3' == duplicateSyscall[3] && '\0' == duplicateSyscall[4])
    {
        duplicateFd = mydup3(applicationFd, pinNativeFd, 0);
    }
    else
    {
        fprintf(stderr, "Unknown duplicate syscall %s\n", duplicateSyscall);
        (void)close(applicationFd);
        return -1;
    }

    if (pinNativeFd != duplicateFd)
    {
        perror("duplicate syscall");
        (void)close(applicationFd);
        return -1;
    }

    struct stat statBufDuplicate;
    if (fstat(duplicateFd, &statBufDuplicate) == -1)
    {
        perror("fstat duplicateFd");
        (void)close(applicationFd);
        (void)close(duplicateFd);
        return -1;
    }

    struct stat statBufApplication;
    if (fstat(applicationFd, &statBufApplication) == -1)
    {
        perror("fstat applicationFd");
        (void)close(applicationFd);
        (void)close(duplicateFd);
        return -1;
    }

    // Verify that the duplicate file descriptor refers to the same file as the application file descriptor.
    if (statBufDuplicate.st_dev != statBufApplication.st_dev || statBufDuplicate.st_ino != statBufApplication.st_ino)
    {
        fprintf(stderr, "Duplicate file descriptor does not refer to the same file as the application file descriptor\n");
        (void)close(applicationFd);
        (void)close(duplicateFd);
        return -1;
    }

    if (-1 == close(applicationFd))
    {
        perror("close");
        (void)close(duplicateFd);
        return -1;
    }

    printf("Application %s errno checks succeeded\n", duplicateSyscall);
    printf("Application %s succeeded\n", duplicateSyscall);
    return duplicateFd;
}

int main(int argc, char* argv[])
{
    int dup2Fd = RunDuplicateSyscall("dup2");
    int dup3Fd = RunDuplicateSyscall("dup3");
    if (-1 == dup2Fd || -1 == dup3Fd)
    {
        return 1;
    }
    if (-1 == close(dup2Fd))
    {
        perror("close dup2fd");
        return 1;
    }
    if (-1 == close(dup3Fd))
    {
        perror("close dup3fd");
        return 1;
    }

    return 0;
}