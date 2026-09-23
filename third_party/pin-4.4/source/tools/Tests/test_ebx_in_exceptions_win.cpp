/*
 * Copyright (C) 2012-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Pin runs this application without a Pintool. The test exercises Pin's reconstruction of the application register context
 * when an instruction that uses EBX implicitly raises an exception.
 *
 * TestAccessViolations() initializes the application registers with sentinel values and deliberately faults on cmpxchg8b [edx],
 * xlat, and cmpxchg8b [ebx]. The exception filter verifies each fault site and resumes at the matching assembly label.
 * Assembly then checks that Pin preserved the application register values. The test succeeds after all three faults are handled
 * in order.
 */

#include <stdio.h>
#include <stdlib.h>

#include <windows.h>




/*!
 * @return IP register value in the given exception context
 */
#if defined(TARGET_IA32)
static ULONG_PTR GetIp(CONTEXT* pExceptContext) { return pExceptContext->Eip; }
static VOID SetIp(LPEXCEPTION_POINTERS exceptPtr, ULONG_PTR addr) { exceptPtr->ContextRecord->Eip = addr; }
#elif defined(TARGET_IA32E)
static ULONG_PTR GetIp(CONTEXT* pExceptContext) { return pExceptContext->Rip; }
static VOID SetIp(LPEXCEPTION_POINTERS exceptPtr, ULONG_PTR addr) { exceptPtr->ContextRecord->Rip = addr; }
#else
#error Unsupported architechture
#endif

static void* AsPointer(ULONG_PTR value) { return reinterpret_cast< void* >(value); }

extern "C" unsigned char* GetResumeIp(unsigned char* ip, unsigned int exceptionNumber, const char** instruction);
extern "C" int TestAccessViolations();
int numExceptions = 0;

/*!
 * Exception filter for the ExecuteSafe function: copy the exception record 
 * to the specified structure.
 * @param[in] exceptPtr        pointers to the exception context and the exception 
 *                             record prepared by the system
 * @param[out] pExceptRecord   pointer to the structure that receives the 
 *                             exception record
 * @param[out] pExceptContext  pointer to the structure that receives the 
 *                             exception context
 * @return the exception disposition
 */
static int SafeExceptionFilter(LPEXCEPTION_POINTERS exceptPtr, EXCEPTION_RECORD* pExceptRecord, CONTEXT* pExceptContext)
{
    numExceptions++;
    *pExceptRecord  = *(exceptPtr->ExceptionRecord);
    *pExceptContext = *(exceptPtr->ContextRecord);
    fprintf(stderr,
            "SafeExceptionFilter: Exception code %x Exception address %p Context IP %p \npExceptContext->Eax %p "
            "exceptPtr->ContextRecord->Eax %p\npExceptContext->Ebx %p exceptPtr->ContextRecord->Ebx %p\npExceptContext->Ecx %p "
            "exceptPtr->ContextRecord->Ecx %p\npExceptContext->Edx %p exceptPtr->ContextRecord->Edx %p\n",
            pExceptRecord->ExceptionCode, pExceptRecord->ExceptionAddress, AsPointer(GetIp(pExceptContext)),
            AsPointer(pExceptContext->Eax), AsPointer(exceptPtr->ContextRecord->Eax), AsPointer(pExceptContext->Ebx),
            AsPointer(exceptPtr->ContextRecord->Ebx), AsPointer(pExceptContext->Ecx), AsPointer(exceptPtr->ContextRecord->Ecx),
            AsPointer(pExceptContext->Edx), AsPointer(exceptPtr->ContextRecord->Edx));
    // Continue execution at the label following the exception-causing instruction.
    ULONG_PTR faultIp = GetIp(pExceptContext);
    const char* instruction;
    unsigned char* resumeIp = GetResumeIp((unsigned char*)faultIp, numExceptions, &instruction);
    if (resumeIp == 0)
    {
        fprintf(stderr, "***Error unexpected exception at ip %p\n", AsPointer(faultIp));
        exit(1);
    }
    fprintf(stderr, "exception at: %s\n", instruction);
    fprintf(stderr, " setting resume ip to %p\n", static_cast< void* >(resumeIp));
    SetIp(exceptPtr, (ULONG_PTR)resumeIp);
    return EXCEPTION_CONTINUE_EXECUTION; // EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char* argv[])
{
    int retVal;
    EXCEPTION_RECORD exceptRecord;
    CONTEXT exceptContext;
    __try
    {
        retVal = TestAccessViolations();
    }
    __except (SafeExceptionFilter((LPEXCEPTION_POINTERS)GetExceptionInformation(), &exceptRecord, &exceptContext))
    {
        fprintf(stderr, "Exception handler: Exception code %x Exception address %p Context IP %p\n", exceptRecord.ExceptionCode,
                exceptRecord.ExceptionAddress, AsPointer(GetIp(&exceptContext)));
    }
    if (!retVal || numExceptions != 3)
    {
        fprintf(stderr, "***Error\n");
        exit(1);
    }
}
