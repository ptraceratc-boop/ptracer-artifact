/*
 * Copyright (C) 2022-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstring>

int main()
{
    pid_t pid = getpid();
    std::cout << std::hex << "return = 0x" << getsid(pid) << std::endl;
    getsid(pid);
    std::cout << "errno = " << strerror(errno) << std::endl;
    return 0;
}
