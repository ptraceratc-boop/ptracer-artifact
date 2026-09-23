/*
 * Copyright (C) 2012-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

extern "C"
{
    extern unsigned char Cmpxchg8bEdxFault;
    extern unsigned char Cmpxchg8bEdxResume;
    extern unsigned char XlatFault;
    extern unsigned char XlatResume;
    extern unsigned char Cmpxchg8bEbxFault;
    extern unsigned char Cmpxchg8bEbxResume;

    // Validate the next expected fault and return the assembly label immediately after the faulting instruction.
    unsigned char* GetResumeIp(unsigned char* ip, unsigned int exceptionNumber, const char** instruction)
    {
        if (exceptionNumber == 1 && ip == &Cmpxchg8bEdxFault)
        {
            *instruction = "cmpxchg8b [edx]";
            return &Cmpxchg8bEdxResume;
        }
        if (exceptionNumber == 2 && ip == &XlatFault)
        {
            *instruction = "xlat";
            return &XlatResume;
        }
        if (exceptionNumber == 3 && ip == &Cmpxchg8bEbxFault)
        {
            *instruction = "cmpxchg8b [ebx]";
            return &Cmpxchg8bEbxResume;
        }
        return 0;
    }
}