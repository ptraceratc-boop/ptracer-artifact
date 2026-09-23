/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#
# This application generates executes a FAR CALL Indirect via Memory that fails.
# far_ptr is a placeholder for where the target pointer is written to.
# The target pointer is written to far_ptr and is a 10-byte pointer which is
# an 8-byte offset plus a 2-byte segment selector. 
# In this app the 2-byte segment selector all zeros, which will result with a #GP.
#

.section .data
.align 64
far_ptr:  .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00, 0x00
.section .text
.global _start
_start:
  leaq exit_syscall(%rip), %rax     # Resolve the target address into rax
  movq %rax, far_ptr(%rip)          # Store the address into far_ptr+0
  rex64 lcall *far_ptr(%rip)        # Perform the far call
exit_syscall:
  mov $60, %rax                     # syscall number for sys_exit
  xor %rdi, %rdi                    # exit code 0
  syscall                           # call kernel
