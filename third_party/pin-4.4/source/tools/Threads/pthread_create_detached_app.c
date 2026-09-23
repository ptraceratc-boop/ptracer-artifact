/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

static bool detachedThreadExited = false;

// Function to be executed by the thread
void* thread_function(void* arg) {
    printf("Detached Thread is running...\n");
    sleep(1); // Simulate some work
    printf("Detached Thread is finishing...\n");
    detachedThreadExited = true;
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_attr_t attr;

    // Initialize the thread attribute
    if (pthread_attr_init(&attr) != 0) {
        perror("pthread_attr_init");
        return EXIT_FAILURE;
    }

    // Set the thread attribute to create the thread in a detached state
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0) {
        perror("pthread_attr_setdetachstate");
        pthread_attr_destroy(&attr);
        return EXIT_FAILURE;
    }

    // Create the thread
    if (pthread_create(&thread, &attr, thread_function, NULL) != 0) {
        perror("pthread_create");
        pthread_attr_destroy(&attr);
        return EXIT_FAILURE;
    }

    // Destroy the thread attribute
    if (pthread_attr_destroy(&attr) != 0) {
        perror("pthread_attr_destroy");
        return EXIT_FAILURE;
    }

    // Main thread continues to run independently of the created thread
    printf("Main thread is continuing...\n");

    // Sleep to allow the detached thread to complete its work
    sleep(3);

    assert(detachedThreadExited);

    printf("Main thread is finishing...\n");
    return EXIT_SUCCESS;
}