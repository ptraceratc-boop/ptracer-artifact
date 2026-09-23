/*
 * Copyright (C) 2014-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <atomic>
#include <iostream>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <cstdlib>

std::atomic< uint32_t > threadCount {0};
volatile bool go                   = false;
std::atomic< uint32_t > stillRunning {0};
volatile bool done                 = false;

pid_t GetTid() { return syscall(__NR_gettid); }

extern "C"
{
    void SecondaryThreadInit(unsigned int threadNum, pid_t tid)
    {
        stillRunning.store((uint32_t)tid);
        std::cout << "APP: Thread #" << threadNum << " (" << tid << ") started." << std::endl << std::flush;
    }

    static void SecondaryThreadWork()
    {
        stillRunning.store((uint32_t)GetTid());
        sched_yield();
    }

    void SecondaryThreadFini(unsigned int threadNum, pid_t tid)
    {
        stillRunning.store((uint32_t)tid);
        std::cout << "APP: Thread #" << threadNum << " (" << tid << ") finished." << std::endl << std::flush;
    }

} // extern "C"

static void* SecondaryThreadMain(void* v)
{
    unsigned int threadNum = threadCount++;
    pid_t tid              = GetTid();

    // Per-thread init
    SecondaryThreadInit(threadNum, tid);
    while (!go)
        sched_yield();

    // Stress test
    for (unsigned int i = 0; i < 1000; ++i)
    {
        SecondaryThreadWork();
    }

    // Per-thread fini
    SecondaryThreadFini(threadNum, tid);
    return NULL;
}

static void* MonitorThreadMain(void* v)
{
    // Make sure that the test is not deadlocked:
    // When a thread completes a step, it assigns a non-zero value to "stillRunning" (tid or pthread id).
    // If the monitor thread wakes up and finds that "stillRunning" is zero, it assumes a deadlock and
    // terminates the application. After a successful check ("stillRunning" is true), the monitor thread
    // resets "stillRunning" to false. A 10-second sleep period should suffice even on the most overloaded
    // system.
    //
    // Special case: The monitor thread sleeps for one 10-second period before it begins to monitor the
    // application to allow the main thread to begin its work creating threads.
    sleep(10);
    unsigned int i = 0;
    while (!done)
    {
        volatile unsigned int isRunning = stillRunning.load();
        std::cout << "APP: monitor iteration " << ++i << ", stillRunning: " << stillRunning << std::endl << std::flush;
        if (0 == isRunning)
        {
            std::cerr << "APP ERROR: Timeout reached" << std::endl;
            exit(5);
        }
        stillRunning.store(0);
        sleep(10);
    }
    return NULL;
}

extern "C"
{
    void ReleaseThreads(volatile bool* doRelease)
    {
        if (false == go)
        {
            std::cerr << "APP ERROR: The tool should have instrumented ReleaseThreads and released the threads" << std::endl;
            exit(3);
        }
    }
} // extern "C"

static void CreateThreads()
{
    static const unsigned int numOfThreads = 50;
    pthread_t tids[numOfThreads];
    for (unsigned int i = 0; i < numOfThreads; ++i)
    {
        if (0 != pthread_create(&tids[i], NULL, SecondaryThreadMain, (void*)i))
        {
            std::cerr << "APP ERROR: Failed to create secondary thread #" << i << std::endl;
            exit(2);
        }
        stillRunning.store((uint32_t)tids[i]);
    }
    std::cout << "APP: All threads created successfully, waiting for them to be ready." << std::endl;
    while (threadCount < numOfThreads)
        sched_yield();
    ReleaseThreads(&go);
    std::cout << "APP: All threads are ready, waiting for them to exit." << std::endl;
    for (unsigned int i = 0; i < numOfThreads; ++i)
    {
        if (0 != pthread_join(tids[i], NULL))
        {
            std::cerr << "APP ERROR: Secondary thread #" << i << " failed to join" << std::endl;
            exit(4);
        }
        stillRunning.store((uint32_t)tids[i]);
    }
    done = true;
}

int main()
{
    pthread_t monitor;
    if (0 != pthread_create(&monitor, NULL, MonitorThreadMain, NULL))
    {
        std::cerr << "APP ERROR: Failed to create the monitor thread" << std::endl;
        exit(1);
    }
    CreateThreads();
    std::cout << "APP: Test completed successfully." << std::endl;
    return 0;
}
