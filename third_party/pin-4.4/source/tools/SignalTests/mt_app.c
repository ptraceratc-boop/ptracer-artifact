/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/**
 * @file mt_app.c
 * @brief An application that is designed to allow intercepting the thread proc and another sub-function
 * 
 * Currently this application is used to test MT signals by async_mt_timers.cpp
 * 
 * @version 0.1
 * @date 2024-07-08
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#include <sched.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#define MAXTHREADS 1000

__attribute__ ((noinline)) void my_yield() 
{
    // Here to do nothing nad be probed by pintool
    sched_yield();
}

void* my_start(void* arg)
{
    int i = 0;
    for (i = 0; i < 1000000; i++)
    {
        my_yield();
    }
    return 0;
}

int main(int argc, char* argv[])
{
    pthread_t threads[MAXTHREADS];
    int i;
    int numthreads = 20;

    if(argc > 1)
    {
        numthreads = atoi(argv[1]);
        if(numthreads > MAXTHREADS)
        {
            numthreads = MAXTHREADS;
        }
        else if(numthreads < 0)
        {
            numthreads = 20;
        }
    }

    printf("Creating %d threads\n", numthreads);

    for (i = 0; i < numthreads; i++)
    {
        pthread_create(&threads[i], 0, my_start, 0);
    }

    for (i = 0; i < numthreads; i++)
    {
        pthread_join(threads[i], 0);
    }
    printf("All threads joined\n");

    return 0;
}
