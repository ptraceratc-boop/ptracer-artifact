/*
 * Copyright (C) 2005-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void handle(int, siginfo_t*, void*);
void make_segv();
size_t getpagesize();

int main()
{
    struct sigaction sigact;

    sigact.sa_sigaction = handle;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sigact, 0) == -1)
    {
        fprintf(stderr, "Unable handle signal\n");
        return 1;
    }

    make_segv();
    return 0;
}

void handle(int sig, siginfo_t* info, void* vctxt)
{
    printf("Got signal %d\n", sig);
    exit(0);
}

void make_segv()
{
    volatile int* p;
    int i;

    p = (volatile int*)0x9;
    i = *p;
}
