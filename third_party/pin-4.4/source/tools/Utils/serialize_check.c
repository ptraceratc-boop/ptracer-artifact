/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "supports_serialize.h"

int main()
{
    if (ProcessorSupportsSerialize())
    {
        printf("Yes\n");
    }
    else
    {
        printf("No\n");
    }
    return 0;
}
