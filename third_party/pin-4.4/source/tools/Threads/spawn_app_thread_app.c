/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * A trivial application that does nothing, but contains function that loops forever
 * which is invoked by Pin's aplication thread creation routine.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#ifdef _WIN32
    #include <windows.h>
    #define sleep(x) Sleep((x) * 1000)  // Convert seconds to milliseconds
#else
    #include <sys/syscall.h> 
    #include <unistd.h>
    #include <sched.h>
#endif

bool wasSpawneAppThreadExecuted = false;

/**
 * This function is being called from the tool using  PIN_SpawnApplicationThread().
 * See tool file comments.
 */
void doNothing()
{
    volatile int loopCount = 0;
    
    wasSpawneAppThreadExecuted = true;

    // !!!! NOTE !!!!
    // current function is being called from the tool using PIN_SpawnApplicationThread().
    // Currently this API does not create application TLS (it doesn't create it and even if it did it would need to be usable)
    // Therefore can't use some libc functions like this which seem to use TLS.
    // printf("MAGIC - hello from spawned applciation thread\n");

    for (;;)
    {
        // see comment about
        // sched_yield();
        // int res        = syscall(__NR_sched_yield); // syscall() can't be used as well.
        loopCount++;
    }
}

void (*funcPtr)();

int main(int argc, char** argv)
{
    printf("hello from main app thread\n");
    // Ensure that the compiler thinks there is a reference to doNothing.
    funcPtr = doNothing;
    sleep(2);
    if (wasSpawneAppThreadExecuted)
    {
        printf("MAGIC - Spawned application thread executed\n");
    }
    else
    {
        printf("Spawned application thread not executed\n");
        exit(-1);
    }

    printf("main thread: exit(0)\n");
    exit(0);
}
