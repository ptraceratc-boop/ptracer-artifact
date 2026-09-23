/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Simple application that waits indefinitely.
 * Used for testing Pin attach mode.
 */

#include <windows.h>
#include <stdio.h>

volatile int keep_running = 1;

BOOL WINAPI console_handler(DWORD signal)
{
    switch (signal)
    {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            keep_running = 0;
            return TRUE;
        default:
            return FALSE;
    }
}

int main(void)
{
    SetConsoleCtrlHandler(console_handler, TRUE);

    while (keep_running)
    {
        Sleep(1000);
    }

    return 0;
}
