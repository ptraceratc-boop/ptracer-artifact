/*
 * Copyright (C) 2011-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

void* functionC(void* p0);

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

int main()
{
    int res;
    pthread_t thread1;

    int param;
    if ((res = pthread_create(&thread1, NULL, &functionC, &param)))
    {
        printf("Thread creation failed: %d\n", res);
        return 1;
    }
    pthread_join(thread1, NULL);
    printf("\n\nApplication finished successfully\n\n");
    return 0;
}

void* functionC(void* p0)
{
    pthread_exit(p0);
    printf("ERROR: This code comes after pthread_exit. It should not be executed!\n");
    assert(0);
}
