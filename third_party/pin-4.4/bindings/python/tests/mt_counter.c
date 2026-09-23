/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define NUM_INC_THREADS 100
#define NUM_DEC_THREADS 20
#define COUNT_PER_THREAD 1000

// Global counter
volatile uint64_t global_counter = 0;

// Mutex for locking the counter
pthread_mutex_t counter_mutex;

// Function that increments the global counter
void increment_counter() {
    pthread_mutex_lock(&counter_mutex);
    global_counter++;
    pthread_mutex_unlock(&counter_mutex);
}

// Function that decrements the global counter
void decrement_counter() {
    pthread_mutex_lock(&counter_mutex);
    global_counter--;
    pthread_mutex_unlock(&counter_mutex);
}

void* increment_thread(void *arg)
{
    for (int i = 0; i < COUNT_PER_THREAD; ++i) {
        increment_counter();
    }
    return NULL;
}

void* decrement_thread(void *arg)
{
    for (int i = 0; i < COUNT_PER_THREAD; ++i) {
        decrement_counter();
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    bool isFuncReplacedByPin = false;
    // Check if "true" argument is provided as first arg
    if (argc > 1) {
        // Check if the first argument is a string representing "true"
        if (0 == strcmp(argv[1], "true")) {
            isFuncReplacedByPin = true;
        }
    }

    pthread_t inc_threads[NUM_INC_THREADS];
    pthread_t dec_threads[NUM_DEC_THREADS];

    // Initialize the mutex
    if (pthread_mutex_init(&counter_mutex, NULL) != 0) {
        printf("Mutex init has failed\n");
        return 1;
    }

    // Create threads to increment the counter
    for (int i = 0; i < NUM_INC_THREADS; ++i) {
        if (pthread_create(&inc_threads[i], NULL, &increment_thread, NULL) != 0) {
            printf("Failed to create increment thread\n");
            return 1;
        }
    }

    // Create threads to decrement the counter
    for (int i = 0; i < NUM_DEC_THREADS; ++i) {
        if (pthread_create(&dec_threads[i], NULL, &decrement_thread, NULL) != 0) {
            printf("Failed to create decrement thread\n");
            return 1;
        }
    }

    // Wait for all increment threads to finish
    for (int i = 0; i < NUM_INC_THREADS; ++i) {
        pthread_join(inc_threads[i], NULL);
    }

    // Wait for all decrement threads to finish
    for (int i = 0; i < NUM_DEC_THREADS; ++i) {
        pthread_join(dec_threads[i], NULL);
    }

    // Destroy the mutex
    pthread_mutex_destroy(&counter_mutex);


    printf("Final global counter value: %" PRIu64 "\n", global_counter);

    if(isFuncReplacedByPin)
    {
        assert(0 == global_counter);
    }
    else
    {
        uint64_t expected_global_counter = (long long)NUM_INC_THREADS * COUNT_PER_THREAD - (long long)NUM_DEC_THREADS * COUNT_PER_THREAD;
        printf("Expected global counter value: %" PRIu64 "\n", expected_global_counter);

        // Assert that the global counter is equal to 80M
        assert(expected_global_counter == global_counter);
    }

    return 0;
}
