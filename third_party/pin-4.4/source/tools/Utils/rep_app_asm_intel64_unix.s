/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

        .text                                   # Place code in the executable text section.

        .globl ExecuteRepInstruction            # Export symbol so C code can call this function.
        .type ExecuteRepInstruction, @function  # Mark symbol as a function for debuggers/linker tools.
ExecuteRepInstruction:                          # Function entry point.
        mov     %rdx, %rcx                      # SysV arg3 (RDX len) -> RCX for REP MOVSB counter.
        cld                                     # Clear direction flag so string ops auto-increment pointers.
        rep     movsb                           # Copy RCX bytes from [RSI] to [RDI].
        ret                                     # Return to caller.

        .section .note.GNU-stack,"",@progbits   # Mark stack as non-executable for hardened toolchains.