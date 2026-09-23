;
; Copyright (C) 2026-2026 Intel Corporation.
; SPDX-License-Identifier: MIT
;

PUBLIC ExecuteRepInstruction ; Export function symbol for the linker.

.code ; Begin code section.

ExecuteRepInstruction PROC ; Function entry point.
    push rdi     ; Preserve non-volatile RDI (destination register for REP MOVSB).
    push rsi     ; Preserve non-volatile RSI (source register for REP MOVSB).
    mov rdi, rcx ; Windows x64 arg1 destination pointer -> RDI.
    mov rsi, rdx ; Windows x64 arg2 source pointer -> RSI.
    mov rcx, r8  ; Windows x64 arg3 byte count -> RCX repeat counter.
    cld          ; Ensure forward copy direction for string operation.
    rep movsb    ; Copy RCX bytes from [RSI] to [RDI].
    pop rsi      ; Restore saved RSI.
    pop rdi      ; Restore saved RDI.
    ret          ; Return to caller.
ExecuteRepInstruction ENDP ; End of function.

end ; End of translation unit.