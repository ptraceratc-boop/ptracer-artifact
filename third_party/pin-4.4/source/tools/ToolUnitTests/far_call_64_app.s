/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#
# This application executes a FAR CALL instruction (lcall) indirect via memory successfully.
# The first FAR CALL (target address in far_ptr1) uses a 64 bit offset.
# The second FAR CALL (target address in far_ptr2) uses a 32 bit offset.
#

.section .data
.align 64
far_ptr1:  .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00, 0x00
far_ptr2:  .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00, 0x00
.section .text
.global _start
_start:

farcall_64offset:
  leaq farcall_32offset(%rip), %rax # Resolve the target address into rax
  mov %cs, %cx
  mov %cx, far_ptr1+8(%rip)         # Store the upper 2 bytes of %cs into far_ptr1+8
  movq %rax, far_ptr1(%rip)         # Store the address into far_ptr1+0
  rex64 lcall *far_ptr1(%rip)       # Perform the far call
farcall_32offset:
  leaq exit_syscall(%rip), %rax     # Resolve the target address into rax
  mov %cx, far_ptr2+4(%rip)         # Store the upper 2 bytes of %cs into far_ptr2+4
  mov %eax, far_ptr2(%rip)          # Store the address into far_ptr2+0
  lcall *far_ptr2(%rip)             # Perform the far call
exit_syscall:
  mov $60, %rax                     # syscall number for sys_exit
  xor %rdi, %rdi                    # exit code 0
  syscall                           # call kernel

