/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <cstdio>
#include <cstdint>

extern "C"
{
    volatile int result = 0; // Global variables
    uint32_t dummy_mem  = 0;

    void large_branch_test(); // Function declaration
}

int main()
{
    printf("Starting execution of large_branch_test...\n");

    large_branch_test();

    printf("Completed all instructions with 20 labels!\n");
    printf("Result: %d\n", result);

    return 0;
}