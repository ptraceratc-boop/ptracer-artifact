/*
 * Copyright (C) 2014-2014 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define NAME(x) x

// This assembly file should built to an executable file
// It tests the correctness of jcxz instruction and exits
// with status 0 if everything's OK.

.global NAME(main)

NAME(main):
   mov $0x10000, %ecx
   xor %eax,%eax
   jcxz test_pass
   mov $1, %al
test_pass:
   ret

