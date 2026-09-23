/*
 * Copyright (C) 2011-2019 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

    .text
.globl BareExit
BareExit:
#if defined(TARGET_LINUX)
    movl    $0,%ebx     # first argument: exit code
    movl    $1,%eax     # system call number (sys_exit) - exit thread
#endif


.globl BareExitTrap
BareExitTrap:
    int     $0x80       # call kernel
