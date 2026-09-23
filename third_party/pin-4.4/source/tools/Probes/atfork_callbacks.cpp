/*
 * Copyright (C) 2015-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include "pin.H"
#include <sys/mman.h>

#include "atfork_callbacks.h"

#include "../Utils/shared_memory.h"



/**
 * This tool communicates with application using shared memory object (See get_shared_object())
 *
 * This pintool (atfork_callbacks.cpp) also prints numbers for the callbacks from PIN.
 * The app (atfork_callbacks_app.cpp) also prints numbers for each of the pthreads callbacks.
 *
 * The before-fork callbacks are atfork_prepare() and pintool_on_fork_before()
 * atfork_prepare() should precede pintool_on_fork_before()
 *
 * The after-fork callbacks for PARENT are atfork_parent() and pintool_on_fork_after_in_parent()
 * pintool_on_fork_after_in_parent() should precede after-fork()
 *
 * The after-fork callbacks for CHILD are atfork_child() and pintool_on_fork_after_in_child()
 * pintool_on_fork_after_in_child() should precede atfork_child()
 */

static size_t objectSize = sizeof(fork_callbacks);

static void pintool_on_fork_before(UINT32 pid, VOID* param)
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getpid());
    ASSERTX(callbacks);

    if (callbacks->atfork_before == 0)
    {
        std::cerr << "pintool_on_fork_before() was called before atfork_prepare()" << std::endl;
        remove_shared_object(getpid());
        PIN_ExitProcess(1);
    }
    callbacks->pin_before_fork = 1;
}

static void pintool_on_fork_after_in_parent(UINT32 pid, VOID* param)
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getpid());
    ASSERTX(callbacks);

    if (callbacks->atfork_after_parent == 1)
    {
        std::cerr << "atfork_parent() was called before pintool_on_fork_after_in_parent()" << std::endl;
        remove_shared_object(getppid());
        PIN_ExitProcess(1);
    }
    callbacks->pin_after_fork_parent = 1;
}

static void pintool_on_fork_after_in_child(UINT32 pid, VOID* param)
{
    fork_callbacks* callbacks = (fork_callbacks*)get_shared_object(objectSize, getppid());
    ASSERTX(callbacks);

    if (callbacks->atfork_after_child == 1)
    {
        std::cerr << "atfork_parent() was called before pintool_on_fork_after_in_parent()" << std::endl;
        remove_shared_object(getppid());
        PIN_ExitProcess(1);
    }
    callbacks->pin_after_fork_child = 1;
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv)) return 1;

    PIN_AddForkFunctionProbed(FPOINT_BEFORE, pintool_on_fork_before, 0);
    PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_PARENT, pintool_on_fork_after_in_parent, 0);
    PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_CHILD, pintool_on_fork_after_in_child, 0);

    PIN_StartProgramProbed();

    return 0;
}
