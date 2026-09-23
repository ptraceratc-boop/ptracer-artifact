;
; Copyright (C) 2025-2025 Intel Corporation.
; SPDX-License-Identifier: MIT
;
include asm_macros.inc

PROLOGUE

.code

; External variables from C++
EXTERN result:DWORD
EXTERN dummy_mem:DWORD

; Macro definitions
COMMON_CODE MACRO
    mov eax, 0
    mov ebx, 1
    mov ecx, DWORD PTR [dummy_mem]
    cmp eax, ebx
ENDM

INS_BLOCK_0 MACRO
    COMMON_CODE
    jz label_0
ENDM

INS_BLOCK_1 MACRO
    COMMON_CODE
    jz label_1
ENDM

INS_BLOCK_2 MACRO
    COMMON_CODE
    jz label_2
ENDM

INS_BLOCK_3 MACRO
    COMMON_CODE
    jz label_3
ENDM

INS_BLOCK_4 MACRO
    COMMON_CODE
    jz label_4
ENDM

INS_BLOCK_5 MACRO
    COMMON_CODE
    jz label_5
ENDM

INS_BLOCK_6 MACRO
    COMMON_CODE
    jz label_6
ENDM

INS_BLOCK_7 MACRO
    COMMON_CODE
    jz label_7
ENDM

INS_BLOCK_8 MACRO
    COMMON_CODE
    jz label_8
ENDM

INS_BLOCK_9 MACRO
    COMMON_CODE
    jz label_9
ENDM

INS_BLOCK_10 MACRO
    COMMON_CODE
    jz label_10
ENDM

INS_10_BLOCKS MACRO
    INS_BLOCK_0
    INS_BLOCK_1
    INS_BLOCK_2
    INS_BLOCK_3
    INS_BLOCK_4
    INS_BLOCK_5
    INS_BLOCK_6
    INS_BLOCK_7
    INS_BLOCK_8
    INS_BLOCK_9
    INS_BLOCK_10
ENDM

INS_50_BLOCKS MACRO
    INS_10_BLOCKS
    INS_10_BLOCKS
    INS_10_BLOCKS
    INS_10_BLOCKS
    INS_10_BLOCKS
ENDM

large_branch_test PROC
    ; Save registers
IFDEF TARGET_IA32E
    push rbx
    push rcx
ELSE
    push ebx
    push ecx
ENDIF

    ; Generate instruction sequences with labels
    INS_50_BLOCKS
label_0:
    nop
    
    INS_50_BLOCKS
label_1:
    nop
    
    INS_50_BLOCKS
label_2:
    nop
    
    INS_50_BLOCKS
label_3:
    nop
    
    INS_50_BLOCKS
label_4:
    nop
    
    INS_50_BLOCKS
label_5:
    nop
    
    INS_50_BLOCKS
label_6:
    nop
    
    INS_50_BLOCKS
label_7:
    nop
    
    INS_50_BLOCKS
label_8:
    nop
    
    INS_50_BLOCKS
label_9:
    nop
    
    INS_50_BLOCKS
label_10:
    ; Store result
    mov DWORD PTR [result], 123
    jmp done_label
    
done_label:
    ; Restore registers
IFDEF TARGET_IA32E
    pop rcx
    pop rbx
ELSE
    pop ecx
    pop ebx
ENDIF
    ret
    
large_branch_test ENDP

end