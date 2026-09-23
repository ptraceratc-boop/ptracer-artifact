/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>


int main() 
{
    ULONG_PTR ptr = VirtualAlloc(NULL, 0x3000, MEM_COMMIT|MEM_RESERVE,   PAGE_EXECUTE_READWRITE);

    printf("Address: 0x%p\n", ptr);

    LPVOID result = ptr;
    for(ULONG_PTR i = 0;i<0x10000000; i+= 0x1000) 
    {
        result = VirtualAlloc(ptr + i, 0x1000, MEM_COMMIT|MEM_RESERVE,       PAGE_EXECUTE_READWRITE);
        if(result != NULL) break;
    }

    printf("Allocated Address: 0x%p\n",result);
    if(result != ptr)
    {
        return 0; // Success
    }
    printf("result == ptr, Failure.\n");
    return 1;
}