/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

        .text                                   # Place code in the executable text section.

        .globl ExecuteRepInstruction            # Export symbol so C code can call this function.
        .type ExecuteRepInstruction, @function  # Mark symbol as a function for debuggers/linker tools.
ExecuteRepInstruction:                          # Function entry point.
        push    %edi                            # Save caller's EDI because REP MOVSB writes destination in EDI.
        push    %esi                            # Save caller's ESI because REP MOVSB reads source from ESI.
        mov     12(%esp), %edi                  # Load arg1: destination pointer (after two pushes).
        mov     16(%esp), %esi                  # Load arg2: source pointer (after two pushes).
        mov     20(%esp), %ecx                  # Load arg3: byte count into ECX counter register.
        cld                                     # Clear direction flag so string ops auto-increment pointers.
        rep     movsb                           # Copy ECX bytes from [ESI] to [EDI] using REP string copy.
        pop     %esi                            # Restore saved ESI.
        pop     %edi                            # Restore saved EDI.
        ret                                     # Return to caller.

        .section .note.GNU-stack,"",@progbits   # Mark stack as non-executable for hardened toolchains.