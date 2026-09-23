/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  Cross-platform application for probe_reattach.test.
 *  Runs indefinitely (stopped by Pin via PIN_ExitProcess) with multiple threads
 *  continuously calling probeable extern "C" functions.
 *
 *  Functions are marked noinline and perform real work to guarantee they are
 *  large enough for a probe (>= 5/6 bytes at entry) on all platforms.
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

// These functions are the probe targets.
// extern "C" ensures the same unmangled name on Linux (GCC/Clang) and Windows (MSVC/clang-cl).

extern "C" NOINLINE void probe_reattach_worker(int iterations)
{
    volatile long long sum = 0;
    for (int i = 0; i < iterations; ++i)
    {
        sum += i;
    }
    // Consume sum to prevent dead-code elimination.
    if (sum < 0)
    {
        fprintf(stderr, "unexpected negative sum\n");
    }
}

extern "C" NOINLINE void probe_reattach_allocator(int size)
{
    void* p = malloc((size_t)size);
    if (p != nullptr)
    {
        volatile char* c = static_cast< volatile char* >(p);
        for (int i = 0; i < size; ++i)
        {
            c[i] = static_cast< char >(i & 0xff);
        }
        free(p);
    }
}

static std::atomic< bool > g_running(true);

static void worker_thread_func()
{
    while (g_running.load(std::memory_order_relaxed))
    {
        probe_reattach_worker(500);
        probe_reattach_allocator(64);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

int main()
{
    const int NUM_THREADS = 4;
    std::thread threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads[i] = std::thread(worker_thread_func);
    }

    // Run until Pin calls PIN_ExitProcess.
    while (g_running.load(std::memory_order_relaxed))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads[i].join();
    }

    return 0;
}
