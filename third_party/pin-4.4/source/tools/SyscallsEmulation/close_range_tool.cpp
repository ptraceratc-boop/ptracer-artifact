/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Opens Pin-owned file descriptors before application execution and verifies
 * that an application close_range() call did not close them.
 */

#include "pin.H"

#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{

constexpr UINT32 kNumberOfFiles = 100;

int g_fds[kNumberOfFiles];
struct stat g_fileStats[kNumberOfFiles];

VOID VerifyPinFileDescriptors(INT32, VOID*)
{
    for (UINT32 i = 0; i < kNumberOfFiles; ++i)
    {
        struct stat fileStat;
        if ((-1 == fstat(g_fds[i], &fileStat)) || (fileStat.st_dev != g_fileStats[i].st_dev) ||
            (fileStat.st_ino != g_fileStats[i].st_ino))
        {
            std::fprintf(stderr, "Pin file descriptor %d was closed\n", g_fds[i]);
            PIN_ExitProcess(1);
        }
    }

    std::printf("Pin close_range succeeded\n");
}

} // namespace

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return 1;
    }

    for (UINT32 i = 0; i < kNumberOfFiles; ++i)
    {
        g_fds[i] = open("/dev/zero", O_RDONLY);
        if (-1 == g_fds[i])
        {
            std::perror("open");
            return 1;
        }

        if (-1 == fstat(g_fds[i], &g_fileStats[i]))
        {
            std::perror("fstat");
            return 1;
        }
    }

    PIN_AddFiniFunction(VerifyPinFileDescriptors, 0);
    PIN_StartProgram();

    return 0;
}