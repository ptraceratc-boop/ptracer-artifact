/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

int main()
{
    printf("fork_app started\n");
    int wstatus = 0;

    for (int i = 0; i < 100; i++)
    {
        pid_t child_id = fork();

        if (child_id == 0)
        {
            // printf("APPLICATION: After fork in child\n");
            exit(0);
        }
        // else
        // {
        //     printf("APPLICATION: After fork in parent\n");
        // }

        // parent
        wait(&wstatus);
        if (!WIFEXITED(wstatus) || 0 != WEXITSTATUS(wstatus))
        {
            printf("One of the forked childs has failed\n");
            exit(-1);
        }
    }

    return 0;
}
