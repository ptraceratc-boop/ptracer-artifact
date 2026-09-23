/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#
# This application executes a FAR CALL instruction (lcall) indirect via memory successfully.
#

.section .data
.align 64
far_ptr:  .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00, 0x00

.section .text
.global _start
_start:
    mov %cs, %cx
    mov %cx, [far_ptr+4]            # Store the upper 2 bytes of %cs into far_ptr+4
    lea exit_syscall, %eax          # Resolve the target address into rax
    mov %eax, [far_ptr]             # Store the address into far_ptr+0
    lcall *[far_ptr]                # Perform the far call
exit_syscall:
    movl    $1,%eax                 # syscall number for sys_exit
    movl    $0,%ebx                 # exit code 0
    int     $0x80                   # call kernel
        

