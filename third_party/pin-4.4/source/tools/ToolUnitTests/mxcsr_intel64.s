/*
 * Copyright (C) 2010-2014 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

.text
.globl GetMxcsr
.type GetMxcsr, function
GetMxcsr: 

	stmxcsr (%rdi)
    ret


.globl SetMxcsr
.type SetMxcsr, function
SetMxcsr: 

	ldmxcsr (%rdi)
    ret
 
