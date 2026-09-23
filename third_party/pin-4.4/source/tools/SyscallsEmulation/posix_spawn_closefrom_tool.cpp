/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Verifies that a file descriptor opened by the tool survives the close-from
 * action in a posix_spawn child immediately before it execs.
 */

#include "pin.H"

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

int g_fd;
struct stat g_fileStat;

BOOL VerifyPinFileDescriptor(CHILD_PROCESS, VOID*)
{
    struct stat fileStat;
    if ((-1 == fstat(g_fd, &fileStat)) || (fileStat.st_dev != g_fileStat.st_dev) || (fileStat.st_ino != g_fileStat.st_ino))
    {
        std::fprintf(stderr, "Pin file descriptor %d was closed before execve\n", g_fd);
        return FALSE;
    }

    std::printf("Pin posix_spawn closefrom succeeded\n");
    std::fflush(stdout);
    return TRUE;
}

} // namespace

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return 1;
    }

    g_fd = open("/dev/zero", O_RDONLY);
    if (-1 == g_fd)
    {
        std::perror("open");
        return 1;
    }
    if (-1 == fstat(g_fd, &g_fileStat))
    {
        std::perror("fstat");
        return 1;
    }

    PIN_AddFollowChildProcessFunction(VerifyPinFileDescriptor, 0);
    PIN_StartProgram();

    return 0;
}