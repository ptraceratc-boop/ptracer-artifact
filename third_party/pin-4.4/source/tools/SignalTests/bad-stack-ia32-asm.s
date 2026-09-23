/*
 * Copyright (C) 2010-2012 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

    .text
.globl DoILLOnBadStack
DoILLOnBadStack:

    movl    %esp, %eax
    movl    $0, %esp
    ud2
    movl    %eax, %esp
    ret

.globl DoSigreturnOnBadStack
DoSigreturnOnBadStack:

    push    %ebp
    movl    %esp, %ebp
#if defined(TARGET_LINUX)
    movl    $0, %esp
    movl    $119, %eax      /* __NR_sigreturn */
    int     $128
#else
#error "Code not defined"
#endif
    movl    %ebp, %esp
    pop     %ebp
    ret
