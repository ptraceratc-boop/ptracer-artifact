/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    LOG("Hello from tool\n");

    PIN_StartProgram();

    return 0;
}
