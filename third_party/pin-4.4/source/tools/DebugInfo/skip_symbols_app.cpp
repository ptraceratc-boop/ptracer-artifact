/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>

extern "C"
{
    static void MyStaticFunction() { std::cout << "Inside MyStaticFunction" << std::endl; }
}

int main()
{
    MyStaticFunction();
    return 0;
}
