;
; Copyright (C) 2012-2026 Intel Corporation.
; SPDX-License-Identifier: MIT
;

PUBLIC TestAccessViolations
PUBLIC Cmpxchg8bEdxFault
PUBLIC Cmpxchg8bEdxResume
PUBLIC XlatFault
PUBLIC XlatResume
PUBLIC Cmpxchg8bEbxFault
PUBLIC Cmpxchg8bEbxResume


.686
.model flat, c
.XMM

.code


TestAccessViolations PROC
    push ebx
    push ebp
    push edi
    push esi 
    xor ebx, ebx
    xor edx, edx

    mov eax, 1234h
    mov ecx, 2345h
    mov ebp, 0abcdh
    mov edi, 0bcdeh
    mov esi, 0cdefh

Cmpxchg8bEdxFault::
    cmpxchg8b QWORD PTR [edx]
Cmpxchg8bEdxResume::
    
    cmp eax, 1234h
    jne ErrorLab
    cmp ecx, 2345h
    jne ErrorLab
    cmp ebx, 0
    jne ErrorLab
    cmp edx, 0
    jne ErrorLab
    cmp ebp, 0abcdh
    jne ErrorLab
    cmp edi, 0bcdeh
    jne ErrorLab
    cmp esi, 0cdefh
    jne ErrorLab
    

    mov eax, 3456h
    mov ecx, 4567h

XlatFault::
    xlat
XlatResume::
    
    cmp eax, 3456h
    jne ErrorLab
    cmp ecx, 4567h
    jne ErrorLab
    cmp ebx, 0
    jne ErrorLab
    cmp edx, 0
    jne ErrorLab
    cmp ebp, 0abcdh
    jne ErrorLab
    cmp edi, 0bcdeh
    jne ErrorLab
    cmp esi, 0cdefh
    jne ErrorLab


    mov eax, 5678h
    mov ecx, 6789h

Cmpxchg8bEbxFault::
    cmpxchg8b QWORD PTR [ebx]
Cmpxchg8bEbxResume::

    cmp eax, 5678h
    jne ErrorLab
    cmp ecx, 6789h
    jne ErrorLab
    cmp ebx, 0
    jne ErrorLab
    cmp edx, 0
    jne ErrorLab
    cmp ebp, 0abcdh
    jne ErrorLab
    cmp edi, 0bcdeh
    jne ErrorLab
    cmp esi, 0cdefh
    jne ErrorLab

    mov eax, 1
    jmp RetLab

ErrorLab:
    mov eax, 0
RetLab:
    pop esi
    pop edi
    pop ebp
    pop ebx	
    ret
TestAccessViolations ENDP
    

end
