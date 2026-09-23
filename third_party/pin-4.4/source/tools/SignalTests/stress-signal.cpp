/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This application is meant to be run with the Pin tool "call-app-stress-tool.cpp".
 * It is a stress test that generates a lot of signals; the signals are caught with a signal
 * handler: Handle(). The handler is configured using the SA_RESTART flag or without: this
 * is controlled using a command option. When SA_RESTART is set, the PIN asynchronous handler
 * uses a flow that calls VMSVC_Xfer(): this is the only way we know to exercise this flow.
 * Several worker threads run in parallel.
 * Those threads wait on a semaphore which is released by the signal handler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <semaphore.h>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>

extern "C" void PIN_TEST_FOO();
extern "C" void PIN_TEST_BAR();
static void Handle(int);

typedef void (*FUN)();

volatile FUN pFoo        = PIN_TEST_FOO;
std::atomic_int SigCount(0);
std::atomic_int sem_wait_interrupted(0);
sem_t global_sem;
constexpr unsigned NOF_INTERRUPTS = 5;
constexpr unsigned SIGNAL_INTERVAL_WAIT_US = 250;
constexpr unsigned SIGNAL_START_WAIT_SEC = 1;
constexpr unsigned NOF_THREADS = 5;
unsigned nofLoop = 200;
//unsigned g_threadIdx = 0;

void blockingFunction()
{
    int loopcount = 0;
    unsigned loop = nofLoop;
    while (loopcount < loop)
    {
        // when returning from sem_wait a signal may have interrupted the call.
        // If this happens, there are two possibilities:
        // . signal handler was registered with SA_RESTART flag
        // . Handler was register w/o SA_RESTART
        // For the first case, kernel restart sem_wait by itself 
        // For the second case, kernel does not restart and sets errno to EINTR
        //  and returns -1. When EINTR is detected we just need to restart sem_wait
        //  by ourself since the semaphore is not yet triggered and it should
        //  happen only when SA_RESTART was not set in main.
        //
        while (-1 == sem_wait(&global_sem) && errno == EINTR)
        {
            sem_wait_interrupted++;
            continue;
        }
        loopcount++;
        printf("*");
        fflush(stdout);
    }
}

int Help(const char *arg)
{
    fprintf(stderr, "illegal argument: %s\n", arg);
    return 1;
}

int main(int argc, char const* argv[])
{
    int sa_restart = 1;
    int threadNo = NOF_THREADS;
    int waitUs = SIGNAL_INTERVAL_WAIT_US;

    /* Parsing arguments */
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--loop"))
        {
            i++;
            nofLoop = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--sa_restart"))
        {
            i++;
            sa_restart = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--thread"))
        {
            i++;
            threadNo = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--waitus"))
        {
            i++;
            waitUs = atoi(argv[i]);
        }
        else
        {
            return Help(argv[i]);
        }
    }

    printf("Number of threads: %d\n", threadNo);
    printf("Loop number per threads: %d\n", nofLoop);
    printf("SA_RESTART: %d\n", sa_restart);

    // initialize semaphore
    if (sem_init(&global_sem, 0, 0) == -1)
    {
        fprintf(stderr, "sem_init error\n");
        return 2;
    }

    // intercept SIGALARM
    struct sigaction act;
    act.sa_handler = Handle;
    act.sa_flags   = (0 == sa_restart) ? 0 : SA_RESTART;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGALRM, &act, 0) != 0)
    {
        fprintf(stderr, "Unable to set ALRM handler\n");
        return 3;
    }

    // trigger SIGALRM periodically
    struct itimerval itval;
    itval.it_interval.tv_sec  = 0;
    itval.it_interval.tv_usec = waitUs;
    itval.it_value.tv_sec     = SIGNAL_START_WAIT_SEC;
    itval.it_value.tv_usec    = 0;
    if (setitimer(ITIMER_REAL, &itval, 0) == -1)
    {
        fprintf(stderr, "Unable to set up timer\n");
        return 4;
    }

    // launch the threads
    std::vector< std::thread > threads(threadNo);
    for (auto& t : threads)
    {
        t = std::thread(blockingFunction);
    }

    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    // stop SIGALRM
    itval.it_value.tv_sec  = 0;
    itval.it_value.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &itval, 0) == -1)
    {
        fprintf(stderr, "Unable to disable timer\n");
        return 5;
    }

    printf("\nsem_wait nof interruptions: %d\n", sem_wait_interrupted.load());
    printf("SigCount=%u\n", SigCount.load());
    fflush(stdout);
    return 0;
}

void PIN_TEST_FOO()
{
    // The Pin tool places an instrumentation point here, which calls PIN_TEST_BAR().
}

void PIN_TEST_BAR() {}

// SIGALRM handler
static void Handle(int sig) 
{
    int val = SigCount++;
    if (val > 0 && 0 == (val % NOF_INTERRUPTS) && -1 == sem_post(&global_sem)) assert(0);
 }
