/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//Child process application
#include <stdio.h>
#include <string>
#include <cstring>
#include <iostream>



int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cout << "Grand child report: expected 2 parameters, received " << argc - 1 << std::endl;
        return -1;
    }
    if (strcmp(argv[1], "param1 param2") || strcmp(argv[2], "param3"))
    {
        std::cout << "Grand child report: wrong parameters: " << argv[1] << " " << argv[2] << std::endl;
        return -1;
    }
    std::cout << "Grand child report: The process works correctly!" << std::endl;
    return 0;
}
