/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//Child process application
#include <windows.h>
#include <stdio.h>
#include <iostream>




int main(int argc, char* argv[])
{
    const int baseArgc = 3;
    if (argc < baseArgc)
    {
        //Got unexpected parameter
        std::cout << argv[0] << ": Some arguments missed" << std::endl;
        return (-2);
    }
    CHAR* expectedArgv[baseArgc] = {{"win_child_process.exe"}, {"param1 param2"}, {"param3"}};
    std::string currentArgv;

    //Take into account that a path might be added to the executable name
    currentArgv             = argv[0];
    std::string::size_type index = currentArgv.find(expectedArgv[0]);
    if (index == std::string::npos)
    {
        //Got unexpected parameter
        std::cout << argv[0] << ": Got unexpected parameter: " << argv[0] << std::endl;
        return (-1);
    }
    //All the rest should have exact match
    for (int i = 1; i < baseArgc; i++)
    {
        currentArgv = argv[i];
        if (currentArgv.compare(expectedArgv[i]) != 0)
        {
            //Got unexpected parameter
            std::cout << argv[0] << ": Got unexpected parameter: " << argv[i] << std::endl;
            return (-1);
        }
    }
    if (argc > baseArgc)
    {
        // Next parameter is sleep interval in milliseconds
        int msecs = atoi(argv[baseArgc]);
        if (msecs > 0)
        {
            Sleep(msecs);
        }
    }
    return 0;
}
