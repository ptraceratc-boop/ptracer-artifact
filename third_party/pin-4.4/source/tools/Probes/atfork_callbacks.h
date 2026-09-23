/*
 * Copyright (C) 2015-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _ATFORK_CALLBACKS_H_
#define _ATFORK_CALLBACKS_H_

/**
 * Structure that is shared between application and tool for testing purposes
 * using shared memory technique.
 */
typedef struct fork_callbacks_s
{
    int atfork_before;
    int atfork_after_child;
    int atfork_after_parent;
    int pin_before_fork;
    int pin_after_fork_child;
    int pin_after_fork_parent;
} fork_callbacks;

#endif // _SHARED_MEMORY_H_
