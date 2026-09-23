/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg.rfind("--", 0) == 0)
        {
            // starts with "--"
            std::string flag = arg.substr(2);
            std::cout << flag << std::endl;
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            // starts with single "-"
            std::string flag = arg.substr(1);
            std::cout << flag << std::endl;
        }
        else
        {
            std::cout << "Unknown argument: " << arg << std::endl;
        }
    }
    std::cout << "Finished application execution." << std::endl;

    return 0;
}