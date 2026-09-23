/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#
# This application generates executes a FAR CALL Indirect via Memory that fails.
# far_ptr is a placeholder for where the target pointer is written to.
# The target pointer is written to far_ptr and is a 6-byte pointer which is
# an 4-byte offset plus a 2-byte segment selector. 
# In this app only the lower 2 bits (the RPL) are copied to the target segment selector while the other bits remain zero.
# On a native run it will segfault but emulated it result with a Pin assert (not supported)
#



.section .data
.align 64
far_ptr:  .byte 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, 0x00, 0x00

.section .text
.global _start
_start:
    mov %cs, %cx
    and $0x3, %cx                   # Mask off all bits except the lower two
    mov %cx, [far_ptr+4]            # Store the upper 2 bytes of %cs into far_ptr+4
    lea exit_syscall, %eax          # Resolve the target address into rax
    mov %eax, [far_ptr]             # Store the address into far_ptr+0
    lcall *[far_ptr]                # Perform the far call
exit_syscall:
    movl    $1,%eax                 # syscall number for sys_exit
    movl    $0,%ebx                 # exit code 0
    int     $0x80                   # call kernel
        

