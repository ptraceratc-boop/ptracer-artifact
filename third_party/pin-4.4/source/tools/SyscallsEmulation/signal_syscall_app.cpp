/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

volatile sig_atomic_t sigusr1_received = 0;

static void handle_sigusr1(int signo)
{
    (void)signo;
    sigusr1_received = 1;

    // Make syscalls inside the signal handler to create nested syscalls.
    [[maybe_unused]] pid_t p = getpid();

    const char msg[] = "SIGUSR1 handler: nested syscalls executed\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

int main(void)
{
    int fds[2];
    if (pipe(fds) != 0)
    {
        perror("pipe");
        return 0;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigusr1;
    sa.sa_flags   = 0; // Do NOT use SA_RESTART so read() returns EINTR
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return 0;
    }

    if (pid == 0)
    {
        // child: short sleep, send signal to parent, then exit
        usleep(100000); // 100ms
        kill(getppid(), SIGUSR1);
        // close write end and exit so parent may see EOF if desired
        close(fds[1]);
        _exit(0);
    }

    // parent: close write end and block on read
    close(fds[1]);
    char buf[64];
    ssize_t n = read(fds[0], buf, sizeof(buf));
    if (n < 0)
    {
        if (errno == EINTR)
        {
            printf("Parent: read interrupted by signal (EINTR)\n");
        }
        else
        {
            perror("read");
        }
    }
    else
    {
        printf("Parent: read returned %zd\n", n);
    }

    waitpid(pid, NULL, 0);
    return 0;
}
