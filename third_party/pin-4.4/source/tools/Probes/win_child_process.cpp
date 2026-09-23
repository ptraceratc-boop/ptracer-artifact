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
    CHAR* expectedArgv[3] = {{"win_child_process.exe"}, {"param1 param2"}, {"param3"}};
    std::string currentArgv;

    //Take into account that a path might be added to the executable name
    currentArgv             = argv[0];
    std::string::size_type index = currentArgv.find(expectedArgv[0]);
    if (index == std::string::npos)
    {
        //Got unexpected parameter
        std::cout << "Got unexpected parameter: " << argv[0] << std::endl;
        return (-1);
    }
    //All the rest should have exact match
    for (int i = 1; i < argc; i++)
    {
        currentArgv = argv[i];
        if (currentArgv.compare(expectedArgv[i]) != 0)
        {
            //Got unexpected parameter
            std::cout << "Got unexpected parameter: " << argv[i] << std::endl;
            return (-1);
        }
    }
    std::cout << "win_child_process - the correct run!" << std::endl;
    return 0;
}
