/*
 * Copyright (C) 2006-2012 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * void CopyWithMovsb(void *src, void *dst, size_t size)
 */
.text
    .align 4
.globl CopyWithMovsb
CopyWithMovsb:

    xchg    %rdi, %rsi  /* src, dst */
    mov     %rdx, %rcx  /* size */
    rep movsb
    ret
