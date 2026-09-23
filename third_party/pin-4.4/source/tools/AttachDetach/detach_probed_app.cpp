/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  Test detaching Pin from running process on a Unix compatible system
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/types.h>

#define NTHREADS 20

#define DLL_SUFFIX ".so"

extern "C" void TellPinToDetach(unsigned long* updateWhenReady) { return; }

extern "C" void SomeFuntionToProbe(int phase)
{
    fprintf(stdout, "Application calling SomeFuntionToProbe(). Phase %d\n", phase);
    fflush(stdout);
}

extern "C" void SomeblockingFuntionToProbe()
{
    // This doesn't do blocking system call, but the the tool will insert a blocking system call (read()) before this function.
    fprintf(stdout, "SomeblockingFuntionToProbe(): Thread was unblocked\n");
    fflush(stdout);

    return;
}

volatile bool loop2 = true;
void* thread_func(void* arg)
{
    while (loop2)
    {
        void* space = malloc(300);
        sleep(1);
        free(space);
    }
    return 0;
}

volatile bool loop1 = true;
typedef double (*SIN_FUNC)(double x);

void* thread_dlopen_func(void* arg)
{
    double number = 0.2;
    while (loop1)
    {
        void* handle = dlopen("libm" DLL_SUFFIX, RTLD_LAZY);
        if (handle)
        {
            SIN_FUNC sin_fptr = (SIN_FUNC)dlsym(handle, "sin");
            if (sin_fptr)
            {
                double val = (*sin_fptr)(number);
                fprintf(stderr, "val = %.4f\n", val);
            }
            sleep(2);
            dlclose(handle);
        }
        number += 0.01;
    }

    return 0;
}
// See SomeblockingFuntionToProbe() documentation
void* thread_blocking_read(void* arg)
{
    SomeblockingFuntionToProbe();
    return 0;
}

int main(int argc, char* argv[])
{
    pthread_t h[NTHREADS];

    bool useBblockingSyscallDuringDetach = false;

    if (2 == argc)
    {
        useBblockingSyscallDuringDetach = true;
    }

    SomeFuntionToProbe(0);

    pthread_create(&h[0], 0, thread_dlopen_func, 0);

    unsigned long startIndex = 1;

    if (useBblockingSyscallDuringDetach)
    {
        pthread_create(&h[1], 0, thread_blocking_read, 0);
        startIndex = 2;
    }

    for (unsigned long i = startIndex; i < NTHREADS; i++)
    {
        pthread_create(&h[i], 0, thread_func, 0);
    }

    SomeFuntionToProbe(1);

    if (useBblockingSyscallDuringDetach)
    {
        // Allow time for tool to start blocking read() in thread_blocking_read thread.
        // It works regardless, but just to be sure that the blocking read() started before doing the detach.
        sleep(1);
    }

    unsigned long pinDetached = false;
    TellPinToDetach(&pinDetached);

    while (!pinDetached)
    {
        sleep(1);
    }

    if (useBblockingSyscallDuringDetach)
    {
        // Allow time for blocking thread to finish its prints after being unblocked
        sleep(1);
    }

    SomeFuntionToProbe(2);

    loop1 = false;
    loop2 = false;

    for (unsigned long i = 0; i < NTHREADS; i++)
    {
        pthread_join(h[i], 0);
    }

    printf("All threads exited. The test PASSED\n");
    return 0;
}
