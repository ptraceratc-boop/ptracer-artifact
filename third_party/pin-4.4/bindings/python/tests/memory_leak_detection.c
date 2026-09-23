/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

void uncalled_func(){
    int *ptr1, *ptr2, *ptr3;

    ptr1 = (int*) malloc(3 * sizeof(int));
    ptr2 = (int*) malloc(4 * sizeof(int));
    ptr3 = (int*) malloc(5 * sizeof(int));

    free(ptr2);
}

void called_func(bool is_to_free_all, int idx_to_free){
    printf("called_func start\n");
    int *ptr1, *ptr2, *ptr3;

    ptr1 = (int*) malloc(3 * sizeof(int));
    ptr2 = (int*) malloc(4 * sizeof(int));
    ptr3 = (int*) malloc(5 * sizeof(int));

    if(is_to_free_all){
        printf("called_func is_to_free_all\n");
        free(ptr1);
        free(ptr2);
        free(ptr3);

        return;
    }
    printf("idx_to_free:%d\n", idx_to_free);
    switch(idx_to_free){
        case 1:
            printf("called_func case 1\n");
            free(ptr1);
            break;
        case 2:
            printf("called_func case 2\n");
            free(ptr2);
            break;
        case 3:
            printf("called_func case 3\n");
            free(ptr3);
            break;
    }
    printf("called_func end\n");
}

int main()
{
    printf("instrumented app is running: %d\n", getpid());
    int *ptr1, *ptr2, *ptr3;

    ptr1 = (int*) malloc(3 * sizeof(int));
    ptr2 = (int*) malloc(4 * sizeof(int));
    ptr3 = (int*) malloc(5 * sizeof(int));

    free(ptr1);

    sleep(1);

    called_func(false, 0);
    called_func(false, 1);
    called_func(false, 2);
    called_func(false, 3);

    called_func(true, 0);
    
    sleep(3);

    printf("instrumented app is ending\n");
    return 0;
}
