/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

 /**
  * @file winapp_stackoverflow.c
  * @brief Test for stack overflow handling in a Windows application.
  *
  * This test checks if the application can handle stack overflow exceptions
  * correctly. It uses _alloca to allocate large amounts of stack space and 
  * verifies if the exception handling mechanism works as expected.
  * The test checks if the stack overflow exception is caught and if the stack can be reset.
  * The test is designed to be run in a Windows environment.
  */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <excpt.h>
#include <windows.h>

#define RET_FINISH 0
#define RET_STACK_EXCEPTION 1
#define RET_OTHER_EXCEPTION 2

#define ARG_CONST1 0xfffffff1
#define ARG_CONST2 0xfffffffffffffff0

__declspec(noinline) int exception_filter() { return EXCEPTION_EXECUTE_HANDLER; }

int foo(int sel)
{
    char* a;
    int ret = RET_FINISH;
    __try
    {
        switch (sel) {
        case 1:  a = (char*)_alloca(ARG_CONST1); break;
        case 2:  a = (char*)_alloca(ARG_CONST2); break;
        default: break;
        }
    }
    __except (exception_filter())
    {
        int i = GetExceptionCode();
        if (i == EXCEPTION_STACK_OVERFLOW)
        {
            if (_resetstkoflw())
                ret = RET_STACK_EXCEPTION;
            else
                ret = RET_OTHER_EXCEPTION;
        }
        else
        {
            ret = RET_OTHER_EXCEPTION;
        }
    }
    return ret;
}

int main()
{
    int cnt = 0;
    if (foo(1) == RET_STACK_EXCEPTION) cnt++;
    if (foo(2) == RET_STACK_EXCEPTION) cnt++;

    if (cnt == 2)
        printf("pass\n");
    else
        printf("fail (%d)\n", cnt);

    return cnt == 2 ? 0 : 1;
}
