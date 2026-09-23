/*
 * Copyright (C) 2015-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// Application that creates new process using popen

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <iostream>
#include <string>





extern char** environ;

//Wait for a process completion
//Verify it returned the expected exit code

int main(int argc, char* argv[])
{
    std::string cmd = argv[1];
    for (int i = 2; i < argc; i++)
        cmd += std::string(" \"") + argv[i] + "\"";
    FILE* pr = popen(cmd.c_str(), "r");
    if (NULL == pr)
    {
        std::cout << "popen failed with " << errno << std::endl;
        return 1;
    }
    char buf[128];
    while (NULL != fgets(buf, sizeof(buf), pr))
    {
        int i = strlen(buf) - 1;
        for (; i >= 0 && isspace(buf[i]); i--)
            ;
        for (; i >= 0; i--)
            std::cout << buf[i];
        std::cout << std::endl;
    }
    return pclose(pr);
}
