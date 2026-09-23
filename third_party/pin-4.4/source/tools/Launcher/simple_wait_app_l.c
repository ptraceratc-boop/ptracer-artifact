/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * Simple application that waits indefinitely.
 * Used for testing Pin attach mode.
 */

#include <signal.h>
#include <unistd.h>

volatile int keep_running = 1;

void signal_handler(int sig)
{
    keep_running = 0;
}

int main()
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    while (keep_running)
    {
        sleep(1);
    }
    
    return 0;
}
