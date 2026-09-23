/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <asm_macros.h>

.data

.text

DECLARE_FUNCTION_AS(large_branch_test)
large_branch_test:
    # Save registers
#if defined(TARGET_IA32E)
    push %rbx
    push %rcx
#else
    push %ebx
    push %ecx
#endif

    
    # Common code block - repeated pattern
    .macro COMMON_CODE
    mov $0, %eax
    mov $1, %ebx
    mov dummy_mem, %ecx
    cmp %ebx, %eax
    .endm
    
    # Instruction blocks with jumps to labels
    .macro INS_BLOCK_0
    COMMON_CODE
    jz label_0
    .endm
    
    .macro INS_BLOCK_1
    COMMON_CODE
    jz label_1
    .endm
    
    .macro INS_BLOCK_2
    COMMON_CODE
    jz label_2
    .endm
    
    .macro INS_BLOCK_3
    COMMON_CODE
    jz label_3
    .endm
    
    .macro INS_BLOCK_4
    COMMON_CODE
    jz label_4
    .endm
    
    .macro INS_BLOCK_5
    COMMON_CODE
    jz label_5
    .endm
    
    .macro INS_BLOCK_6
    COMMON_CODE
    jz label_6
    .endm
    
    .macro INS_BLOCK_7
    COMMON_CODE
    jz label_7
    .endm
    
    .macro INS_BLOCK_8
    COMMON_CODE
    jz label_8
    .endm
    
    .macro INS_BLOCK_9
    COMMON_CODE
    jz label_9
    .endm
    
    .macro INS_BLOCK_10
    COMMON_CODE
    jz label_10
    .endm
    
    .macro INS_BLOCK_11
    COMMON_CODE
    jz label_11
    .endm
    
    .macro INS_BLOCK_12
    COMMON_CODE
    jz label_12
    .endm
    
    .macro INS_BLOCK_13
    COMMON_CODE
    jz label_13
    .endm
    
    .macro INS_BLOCK_14
    COMMON_CODE
    jz label_14
    .endm
    
    .macro INS_BLOCK_15
    COMMON_CODE
    jz label_15
    .endm
    
    .macro INS_BLOCK_16
    COMMON_CODE
    jz label_16
    .endm
    
    .macro INS_BLOCK_17
    COMMON_CODE
    jz label_17
    .endm
    
    .macro INS_BLOCK_18
    COMMON_CODE
    jz label_18
    .endm
    
    .macro INS_BLOCK_19
    COMMON_CODE
    jz label_19
    .endm
    
    # 20 blocks macro
    .macro INS_20_BLOCKS
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
    INS_BLOCK_11
    INS_BLOCK_12
    INS_BLOCK_13
    INS_BLOCK_14
    INS_BLOCK_15
    INS_BLOCK_16
    INS_BLOCK_17
    INS_BLOCK_18
    INS_BLOCK_19
    .endm
    
    # 100 blocks macro
    .macro INS_100_BLOCKS
    INS_20_BLOCKS
    INS_20_BLOCKS
    INS_20_BLOCKS
    INS_20_BLOCKS
    INS_20_BLOCKS
    .endm
    
    # Generate the instruction sequences with labels
    INS_100_BLOCKS
label_0:
    nop
    
    INS_100_BLOCKS
label_1:
    nop
    
    INS_100_BLOCKS
label_2:
    nop
    
    INS_100_BLOCKS
label_3:
    nop
    
    INS_100_BLOCKS
label_4:
    nop
    
    INS_100_BLOCKS
label_5:
    nop
    
    INS_100_BLOCKS
label_6:
    nop
    
    INS_100_BLOCKS
label_7:
    nop
    
    INS_100_BLOCKS
label_8:
    nop
    
    INS_100_BLOCKS
label_9:
    nop
    
    INS_100_BLOCKS
label_10:
    nop
    
    INS_100_BLOCKS
label_11:
    nop
    
    INS_100_BLOCKS
label_12:
    nop
    
    INS_100_BLOCKS
label_13:
    nop
    
    INS_100_BLOCKS
label_14:
    nop
    
    INS_100_BLOCKS
label_15:
    nop
    
    INS_100_BLOCKS
label_16:
    nop
    
    INS_100_BLOCKS
label_17:
    nop
    
    INS_100_BLOCKS
label_18:
    nop
    
    INS_100_BLOCKS
label_19:
    movl $123, result
    jmp done_label
    
done_label:
    # Restore registers
#if defined(TARGET_IA32E)
    pop %rcx
    pop %rbx
#else
    pop %ecx
    pop %ebx
#endif
    ret

END_FUNCTION(large_branch_test)