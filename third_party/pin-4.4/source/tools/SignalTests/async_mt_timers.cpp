/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This pintool is designed to test a highly multithreaded application
 *  where each thread is sent a timer signal periodically.
 *  The timer handler will call gettid() that acquires a read lock inside PINOS.
 *  The function that is being interrupted (on_yield) also calls gettid() making it statistically
 *  probable that gettid() will get interrupted causing a recursive acquiring of the read lock in PINOS.
 *  In the meantime new threads continue to be created and destroyed causing a writer lock in PINOS to
 *  be acquired. Bug PINT-6185 details the deadlock that occurs under these circumstances.
 *  If PINOS was correctly fixed this program should not deadlock.
 * 
 *  This tool requires mt_app (see mt_app.c)
 */

#include "pin.H"

#include <signal.h>
#include <time.h>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <mutex>

#ifdef __NR_timer_settime32
#define __NR_timer_settime __NR_timer_settime32
#endif

std::atomic< bool > handlerSet {false};
std::atomic< unsigned long long > handlerCount {0};
std::atomic< unsigned long long > yieldCount {0};

#define MY_SIG_TIMER (SIGRTMIN + 5)

KNOB< BOOL > KnobUnlinkThreadInSignal(KNOB_MODE_WRITEONCE, "pintool", "unlink_thread", "0",
                                      "Exit the thread in a signal handler trying to cause a reader writer deadlock");

void timer_expired(int sig, siginfo_t* si, void* uc)
{
    auto tid = gettid(); // This call in the signal handler should trigger the PINT-6185

    // Just do something with tid so it doesn't get optimized away
    if (tid)
    {
        ++handlerCount;
        if (KnobUnlinkThreadInSignal)
        {
            OS_Syscall(SYS_unlink_native_thread, 0, nullptr); // Will force acquiring a writer lock
            OS_Syscall(SYS_link_native_thread, 0, nullptr);   // In case we didn't deadlock, make sure we are registered
        }
    }
}

int posix_timer_create(pid_t targetThreadId, timer_t& timerId)
{
    sigevent evt {};

    evt.sigev_notify           = SIGEV_THREAD_ID;
    evt.sigev_signo            = MY_SIG_TIMER;
    evt.sigev_notify_thread_id = targetThreadId;

    auto ret = OS_Syscall_unsafe(__NR_timer_create, CLOCK_REALTIME, &evt, &timerId);
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        printf("__NR_timer_create failed with %s\n", strerror(-ret));
    }
    return (int)PINOS_SYSCALL_RET(ret);
}

int posix_timer_settime(timer_t timerId, uint32_t intervalUs)
{
    itimerspec interval {};
    interval.it_interval.tv_sec  = intervalUs / 1000000;
    interval.it_interval.tv_nsec = (intervalUs % 1000000) * 1000;
    interval.it_value            = interval.it_interval;

    auto ret = OS_Syscall_unsafe(__NR_timer_settime, timerId, 0, &interval, nullptr);
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        printf("__NR_timer_settime failed with %s\n", strerror(-ret));
    }
    return (int)PINOS_SYSCALL_RET(ret);
}

void on_yield()
{
    // The call to gettid() inside the threadproc together with the call
    // in the signal handler trigger the PINT-6185.
    // This will happen because when gettid() is called for the first time ever
    // for an application thread, it will register the thread and acquire a write LOCK
    // in PINOS. A write lock will not be interruptable by a signal. However consecutive
    // calls will only acquire a reader lock which is interruptable by a signal.
    // If enough threads are created then without the solution implemented a deadlock will happen.
    // The deadlock of the original bug is that a thread tries to acquire the writer lock while other threads
    // have their reader lock reacquired in a signal handle that interrupted a read lock. When this happens
    // the writer lock cannot advance while there are readers and the readers cannot advance while a writer
    // is attempting to acquire the lock.
    // If the bug is fixed then this test should finish without a timeout
    auto tid = gettid();
    if (tid)
    {
        ++yieldCount;
        //sched_yield();
    }
}

void on_thread_start()
{
    bool initialized = false;
    if (handlerSet.compare_exchange_strong(initialized, true))
    {
        struct sigaction sa;
        memset((void*)(&sa), 0, sizeof(struct sigaction));
        sa.sa_flags     = SA_SIGINFO;
        sa.sa_sigaction = timer_expired;
        sigemptyset(&sa.sa_mask);
        ASSERTX(0 == sigaction_unfiltered(MY_SIG_TIMER, &sa, nullptr));
    }

    timer_t timerId;
    pid_t tid = PIN_GetNativeTid();

    sigset_t blockSet, unblockSet;
    sigemptyset(&blockSet);
    sigemptyset(&unblockSet);
    sigaddset(&blockSet, MY_SIG_TIMER);
    sigaddset(&blockSet, SIGALRM);
    sigaddset(&unblockSet, MY_SIG_TIMER);

    // Temporarily block MY_SIG_TIMER and permanently block SIGALRM
    ASSERTX(0 == sigprocmask_unfiltered(SIG_BLOCK, &blockSet, NULL));

    ASSERTX(0 == posix_timer_create(tid, timerId));
    ASSERTX(0 == posix_timer_settime(timerId, 2000));

    // Unblock just MY_SIG_TIMER
    ASSERTX(0 == sigprocmask_unfiltered(SIG_UNBLOCK, &unblockSet, NULL));
}

void on_exit()
{
    printf("Yield called %llu times\n", yieldCount.load());
    printf("Handler called %llu times\n", handlerCount.load());
    fflush(stdout);
}

void on_main()
{
    sigset_t blockSet, unblockSet;
    sigemptyset(&blockSet);
    sigemptyset(&unblockSet);
    sigaddset(&blockSet, MY_SIG_TIMER);
    sigaddset(&unblockSet, SIGALRM);

    // block MY_SIG_TIMER for main thread
    ASSERTX(0 == sigprocmask_unfiltered(SIG_BLOCK, &blockSet, NULL));
    // Unblock SIGALRM for main thread
    ASSERTX(0 == sigprocmask_unfiltered(SIG_UNBLOCK, &unblockSet, NULL));

    alarm(180); // This should terminate the process if it is deadlocked
}

void OnImageLoad(IMG img, void*)
{
    fprintf(stderr, "Processing Image %s\n", IMG_Name(img).c_str());
    if (IMG_IsMainExecutable(img))
    {
        auto mainRtn = RTN_FindByName(img, "main");
        ASSERTX(RTN_Valid(mainRtn));
        ASSERTX(RTN_InsertCallProbed(mainRtn, IPOINT_BEFORE, on_main, IARG_END));

        auto startRtn = RTN_FindByName(img, "my_start");
        ASSERTX(RTN_Valid(startRtn));
        ASSERTX(RTN_InsertCallProbed(startRtn, IPOINT_BEFORE, on_thread_start, IARG_END));

        auto yieldRtn = RTN_FindByName(img, "my_yield");
        ASSERTX(RTN_Valid(yieldRtn));
        ASSERTX(RTN_ReplaceProbed(yieldRtn, on_yield));
    }
    else
    {
        auto exitRtn = RTN_FindByName(img, "_exit");
        if (RTN_Valid(exitRtn))
        {
            ASSERTX(RTN_InsertCallProbed(exitRtn, IPOINT_BEFORE, on_exit, IARG_END));
        }
    }
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        printf("Help Me\n");
        return -1;
    }

    IMG_AddInstrumentFunction(OnImageLoad, nullptr);

    // Start the program, never returns
    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
