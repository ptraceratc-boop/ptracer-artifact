/*
 * Copyright (C) 2015-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <iostream>
#include <sys/mman.h>

#include "../Utils/shared_memory.h"
#include "atfork_callbacks.h"



/***********************************************
 *
 * This application communicates with tool using shared memory object
 * (See create_shared_object() & get_shared_object())
 *
 * This app prints a number for each of the pthreads callbacks.
 * The pintool (atfork_callbacks.cpp) also prints numbers for the callbacks from PIN.
 *
 * The before-fork callbacks are atfork_prepare() and pintool_on_fork_before()
 * atfork_prepare() should precede pintool_on_fork_before()
 *
 * The after-fork callbacks for PARENT are atfork_parent() and pintool_on_fork_after_in_parent()
 * pintool_on_fork_after_in_parent() should precede after-fork()
 *
 * The after-fork callbacks for CHILD are atfork_child() and pintool_on_fork_after_in_child()
 * pintool_on_fork_after_in_child() should precede atfork_child()
 *
 ************************************************/

static size_t objectSize = sizeof(fork_callbacks);

void atfork_prepare()
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getpid());
    assert(callbacks);

    if (callbacks->pin_before_fork != 0)
    {
        std::cerr << "pintool_on_fork_before() was called before atfork_prepare()" << std::endl;
        remove_shared_object(getpid());
        exit(1);
    }
    callbacks->atfork_before = 1;
}

void atfork_parent()
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getpid());
    assert(callbacks);

    if (callbacks->pin_after_fork_parent != 1)
    {
        std::cerr << "atfork_parent() was called before pintool_on_fork_after_in_parent()" << std::endl;
        remove_shared_object(getpid());
        exit(1);
    }
    callbacks->atfork_after_parent = 1;
}

void atfork_child()
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getppid());
    assert(callbacks);

    if (callbacks->pin_after_fork_child != 1)
    {
        std::cerr << "atfork_child() was called before pintool_on_fork_after_in_child()" << std::endl;
        remove_shared_object(getppid());
        exit(1);
    }
    callbacks->atfork_after_child = 1;
}

int main()
{
    fork_callbacks* callbacks = (fork_callbacks*)create_shared_object(objectSize, getpid());
    assert(callbacks);
    callbacks->atfork_before         = 0;
    callbacks->atfork_after_child    = 0;
    callbacks->atfork_after_parent   = 0;
    callbacks->pin_before_fork       = 0;
    callbacks->pin_after_fork_child  = 0;
    callbacks->pin_after_fork_parent = 0;

    pthread_atfork(atfork_prepare, atfork_parent, atfork_child);

    pid_t child = fork();

    if (child != 0)
    {
        assert(child > 0);

        int status   = 0;
        pid_t client = wait(&status);
        assert(client == child);

        fork_callbacks callbacks_copy = *callbacks;

        remove_shared_object(getpid());

        assert(callbacks_copy.atfork_before);
        assert(callbacks_copy.atfork_after_child);
        assert(callbacks_copy.atfork_after_parent);
        assert(callbacks_copy.pin_before_fork);
        assert(callbacks_copy.pin_after_fork_child);
        assert(callbacks_copy.pin_after_fork_parent);
    }

    return 0;
}
