/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Requests that posix_spawn() close every descriptor starting at 3 in the
 * spawned process.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

int main(int argc, char* argv[])
{
    if ((argc == 2) && (0 == strcmp(argv[1], "--posix-spawn-closefrom-child")))
    {
        return 0;
    }

    posix_spawn_file_actions_t fileActions;
    int result = posix_spawn_file_actions_init(&fileActions);
    if (0 != result)
    {
        fprintf(stderr, "posix_spawn_file_actions_init failed: %s\n", strerror(result));
        return 1;
    }

    result = posix_spawn_file_actions_addclosefrom_np(&fileActions, STDERR_FILENO + 1);
    if (0 != result)
    {
        fprintf(stderr, "posix_spawn_file_actions_addclosefrom_np failed: %s\n", strerror(result));
        posix_spawn_file_actions_destroy(&fileActions);
        return 1;
    }

    char* const childArgv[] = { argv[0], "--posix-spawn-closefrom-child", NULL };
    pid_t childPid;
    result = posix_spawn(&childPid, childArgv[0], &fileActions, NULL, childArgv, environ);
    posix_spawn_file_actions_destroy(&fileActions);
    if (0 != result)
    {
        fprintf(stderr, "posix_spawn failed: %s\n", strerror(result));
        return 1;
    }

    int status;
    if (-1 == waitpid(childPid, &status, 0))
    {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || (0 != WEXITSTATUS(status)))
    {
        fprintf(stderr, "posix_spawn child exited with status %d\n", status);
        return 1;
    }

    printf("Application posix_spawn closefrom succeeded\n");
    return 0;
}