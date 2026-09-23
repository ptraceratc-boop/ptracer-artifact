;
; Copyright (C) 2026-2026 Intel Corporation.
; SPDX-License-Identifier: MIT
;

PUBLIC ExecuteRepInstruction ; Export function symbol for the linker.

.686           ; Assemble for 32-bit x86 instruction set.
.model flat, c ; Use flat memory model with C calling convention.

.code   ; Begin code section.
ALIGN 4 ; Align function start for predictable instruction fetch.
ExecuteRepInstruction PROC ; Function entry point.
    push edi            ; Preserve caller's EDI because REP MOVSB updates destination pointer.
    push esi            ; Preserve caller's ESI because REP MOVSB updates source pointer.
    mov edi, [esp + 12] ; Load arg1 destination pointer after two pushes.
    mov esi, [esp + 16] ; Load arg2 source pointer after two pushes.
    mov ecx, [esp + 20] ; Load arg3 byte count into ECX repeat counter.
    cld                 ; Ensure forward copy direction for string operation.
    rep movsb           ; Copy ECX bytes from [ESI] to [EDI].
    pop esi             ; Restore saved ESI.
    pop edi             ; Restore saved EDI.
    ret                 ; Return to caller.
ExecuteRepInstruction ENDP ; End of function.

end ; End of translation unit.