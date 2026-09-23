/*
 * Copyright (C) 2006-2006 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define NAME(x) x

.text
    .align 4
.globl NAME(DoInt)
NAME(DoInt):
    int     $12
    ret
