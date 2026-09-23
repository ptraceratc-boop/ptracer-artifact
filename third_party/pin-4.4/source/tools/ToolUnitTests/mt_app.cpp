/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * A simple MT application.
 */

#include <string>
#include <iostream>
#include "../Utils/threadlib.h"




#if defined(TARGET_WINDOWS)
#define EXPORT_CSYM extern "C" __declspec(dllexport)
#else
#define EXPORT_CSYM extern "C"
#endif

/*!
 * The tool intercepts this function and flushes the Code Cache. 
 */
volatile unsigned numFlushes = 0;
EXPORT_CSYM void DoFlush() { ++numFlushes; }

void* ThreadProc(void* arg)
{
    DelayCurrentThread(1000);
    return 0;
}

int main(int argc, char* argv[])
{
    const int numThreads = 3;
    THREAD_HANDLE threads[numThreads];

    for (int i = 0; i < numThreads; i++)
    {
        if (!CreateOneThread(&threads[i], ThreadProc, 0))
        {
            std::cerr << "CreateOneThread failed" << std::endl << std::flush;
        }
    }

    for (int i = 0; i < numThreads; i++)
    {
        if (!JoinOneThread(threads[i]))
        {
            std::cerr << "JoinOneThread failed" << std::endl << std::flush;
        }
    }

    std::cerr << "All application's threads joined" << std::endl << std::flush;

    // Trigger the Code Cache flush
    DoFlush();
    DelayCurrentThread(1000);

    std::cerr << "Application is exiting" << std::endl << std::flush;

    return 0;
}
