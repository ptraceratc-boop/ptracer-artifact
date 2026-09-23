/*
 * Copyright (C) 2011-2019 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

    .text
.globl BareExitGroup
BareExitGroup:
#if defined(TARGET_LINUX)
    movl    $0,%ebx     # first argument: exit code
    movl    $252,%eax   # system call number (sys_exit_group) - exit process
#endif

.globl BareExitTrap
BareExitTrap:
    int     $0x80       # call kernel
