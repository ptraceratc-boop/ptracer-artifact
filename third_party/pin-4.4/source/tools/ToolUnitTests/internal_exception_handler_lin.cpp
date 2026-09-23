/*
 * Copyright (C) 2010-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This test tool verifies correctness of Pin's internal exception handling
 * Note - this tool uses Win32 API after initialization phase (after main()). 
 * This prcatice is unsupported and used here for testing purposes only.
 */

#include "pin.H"
#include <string>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <sys/mman.h>
#include <limits.h> /* for PAGESIZE */
#ifndef PAGESIZE
#define PAGESIZE 4096
#endif
#include <errno.h>
#include <ucontext.h>




#ifdef TARGET_BSD
#include <machine/fpu.h>
#endif

#define FCW_ZERO_DIVIDE 0x4
#define MXCSR_ZERO_DIVIDE 0x200
#ifdef TARGET_BSD
#define FCW_MASK_ZERO_DIVIDE(fpstate) ((reinterpret_cast< struct envxmm* >(fpstate))->en_cw |= FCW_ZERO_DIVIDE)
#define FSW_RESET(fpstate) ((reinterpret_cast< struct envxmm* >(fpstate))->en_sw = 0)
#define MSR_MASK_ZERO_DIVIDE(fpstate) ((reinterpret_cast< struct envxmm* >(fpstate))->en_mxcsr |= MXCSR_ZERO_DIVIDE)
#else
#define FCW_MASK_ZERO_DIVIDE(fpstate) ((reinterpret_cast< FPSTATE* >(fpstate))->fxsave_legacy._fcw |= FCW_ZERO_DIVIDE)
#define FSW_RESET(fpstate) ((reinterpret_cast< FPSTATE* >(fpstate))->fxsave_legacy._fsw = 0)
#define MSR_MASK_ZERO_DIVIDE(fpstate) ((reinterpret_cast< FPSTATE* >(fpstate))->fxsave_legacy._mxcsr |= MXCSR_ZERO_DIVIDE)
#endif

//=================================================================================================
/*!
 * Global variables
 */
volatile BOOL isGlobalExpected1 = FALSE;
volatile ADDRINT* ptr1          = NULL;
volatile BOOL isGlobalExpected2 = FALSE;
volatile ADDRINT* ptr2          = NULL;
volatile BOOL isGlobalExpected3 = FALSE;
extern "C" void ToolRaiseAccessInvalidAddressException(ADDRINT** addr, ADDRINT val);
extern "C" ADDRINT ToolCatchAccessInvalidAddressException;
extern "C" ADDRINT ToolIpAccessInvalidAddressException;

extern "C" ADDRINT ToolRaiseIntDivideByZeroException(ADDRINT catchAddr, ADDRINT code);
extern "C" ADDRINT ToolCatchIntDivideByZeroException;
extern "C" ADDRINT ToolIpIntDivideByZeroException;

typedef std::map< ADDRINT, ADDRINT > IpToCatchBlockMap;
IpToCatchBlockMap ip2catchMap;

//================================================================================
//* FP control
//================================================================================

void X87UnmaskZeroDivide(unsigned char* fpstate) {}
//=================================================================================================
/*!
 * Exception handlers
 */
EXCEPT_HANDLING_RESULT AnalysisHandler2(THREADID threadIndex, EXCEPTION_INFO* pExceptInfo, PHYSICAL_CONTEXT* pPhysCtxt, VOID* v)
{
    std::cout << "AnalysisHandler2: Caught exception. " << PIN_ExceptionToString(pExceptInfo) << std::endl;
    std::cout << "AnalysisHandler2: can't fix exception, continue search" << std::endl;
    return EHR_CONTINUE_SEARCH;
}

EXCEPT_HANDLING_RESULT AnalysisHandler1(THREADID tid, EXCEPTION_INFO* pExceptInfo, PHYSICAL_CONTEXT* pPhysCtxt, VOID* v)
{
    std::cout << "AnalysisHandler1: Caught exception. " << PIN_ExceptionToString(pExceptInfo) << std::endl;
    if (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_INT_DIVIDE_BY_ZERO)
    {
        ADDRINT ip                     = PIN_GetPhysicalContextReg(pPhysCtxt, REG_INST_PTR);
        IpToCatchBlockMap::iterator it = ip2catchMap.find(ip);
        if (it != ip2catchMap.end())
        {
            std::cout << "AnalysisHandler1: Fixing INT_DIVIDE_BY_ZERO and continuing execution" << std::endl;
            ADDRINT catchPoint = it->second;
            PIN_SetPhysicalContextReg(pPhysCtxt, REG_INST_PTR, catchPoint);
            std::cout << "=====================================================================" << std::endl;
            return EHR_HANDLED;
        }
    }
    if (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_X87_DIVIDE_BY_ZERO)
    {
        std::cout << "AnalysisHandler1: Fixing X87_DIVIDE_BY_ZERO and continuing execution" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        CHAR fpState[FPSTATE_SIZE];
        PIN_GetPhysicalContextFPState(pPhysCtxt, fpState);
        FCW_MASK_ZERO_DIVIDE(fpState);
        FSW_RESET(fpState);
        PIN_SetPhysicalContextFPState(pPhysCtxt, fpState);
        return EHR_HANDLED;
    }
    if (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_SIMD_DIVIDE_BY_ZERO)
    {
        std::cout << "AnalysisHandler1: Fixing SIMD_DIVIDE_BY_ZERO and continuing execution" << std::endl;
        std::cout << "=====================================================================" << std::endl;
        CHAR fpState[FPSTATE_SIZE];
        PIN_GetPhysicalContextFPState(pPhysCtxt, fpState);
        MSR_MASK_ZERO_DIVIDE(fpState);
        PIN_SetPhysicalContextFPState(pPhysCtxt, fpState);
        return EHR_HANDLED;
    }
    std::cout << "AnalysisHandler1: can't fix exception, continue search" << std::endl;
    return EHR_CONTINUE_SEARCH;
}

/*
 * Don't catch exception. Just propagate it.
 */
EXCEPT_HANDLING_RESULT GlobalHandler2(THREADID threadIndex, EXCEPTION_INFO* pExceptInfo, PHYSICAL_CONTEXT* pPhysCtxt, VOID* v)
{
    std::cout << "GlobalHandler2: Caught exception. " << PIN_ExceptionToString(pExceptInfo) << std::endl;
    std::cout << "GlobalHandler2: Continue search." << std::endl;
    return EHR_CONTINUE_SEARCH;
}

/*
 * Catch expected exceptions and recover form them
 * (1) access invalid address
 *     - move IP reg to recovery code
 * (2, 3) access denied
 *     - change page protection 
 */
EXCEPT_HANDLING_RESULT GlobalHandler1(THREADID threadIndex, EXCEPTION_INFO* pExceptInfo, PHYSICAL_CONTEXT* pPhysCtxt, VOID* v)
{
    std::cout << "GlobalHandler1: Caught exception. " << PIN_ExceptionToString(pExceptInfo) << std::endl;
    if (isGlobalExpected1 && (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_ACCESS_INVALID_ADDRESS))
    {
        isGlobalExpected1 = FALSE;

        ADDRINT ip = PIN_GetPhysicalContextReg(pPhysCtxt, REG_INST_PTR);
        // fprintf(stderr, "exception at ip 0x%lx\n", (unsigned long)ip);

        /* Move IP to recovery code. The recovery code is provided by tool itself */
        IpToCatchBlockMap::iterator it = ip2catchMap.find(ip);
        if (it != ip2catchMap.end())
        {
            std::cout << "GlobalHandler1: Fixing ACCESS_INVALID_ADDRESS and continue execution." << std::endl;
            ADDRINT catchPoint = it->second;
            PIN_SetPhysicalContextReg(pPhysCtxt, REG_INST_PTR, catchPoint);
        }
        else
        {
            std::cout << "GlobalHandler1 ERROR: can't fix ACCESS_INVALID_ADDRESS" << std::endl;
            exit(-1);
        }

        std::cout << "=====================================================================" << std::endl;
        return EHR_HANDLED;
    }
    else if (isGlobalExpected2 && (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_ACCESS_DENIED))
    {
        isGlobalExpected2 = FALSE;
        std::cout << "GlobalHandler1: Fixing ACCESS_DENIED and continue execution." << std::endl;

        // Change page protection and continue execution
        int res = mprotect((void*)ptr2, 1024, PROT_READ | PROT_WRITE);
        if (res != 0)
        {
            std::cout << "mprotect failed with code " << std::ios::dec << errno << std::endl;
        }
        std::cout << "=====================================================================" << std::endl;
        return EHR_HANDLED;
    }
    else if (isGlobalExpected3 && (PIN_GetExceptionCode(pExceptInfo) == EXCEPTCODE_ACCESS_DENIED))
    {
        isGlobalExpected3 = FALSE;
        std::cout << "GlobalHandler1: Fixing ACCESS_DENIED and continuing execution" << std::endl;

        // Change page protection and continue execution
        int res = mprotect((void*)ptr1, 1024, PROT_READ);
        if (res != 0)
        {
            std::cout << "mprotect failed with code " << std::ios::dec << errno << std::endl;
        }
        std::cout << "=====================================================================" << std::endl;
        return EHR_HANDLED;
    }
    else
    {
        // Message logging
        std::cout << "GlobalHandler1: Unexpected exception. Abort" << std::endl;
        // Stop searching for internal handler.
        return EHR_UNHANDLED;
    }
}

//=================================================================================================
/*!
 * RTN analysis routines.
 */

static ADDRINT OnRaiseIntDivideByZeroException(THREADID tid, ADDRINT exceptionCode)
{
    PIN_TryStart(tid, AnalysisHandler1, 0);

    PIN_TryStart(tid, AnalysisHandler2, 0);

    std::cout << "Raise exception with code " << exceptionCode << std::endl;

    ADDRINT code = ToolRaiseIntDivideByZeroException(ToolCatchIntDivideByZeroException, exceptionCode);

    PIN_TryEnd(tid);

    PIN_TryEnd(tid);

    return code;
}

static ADDRINT OnRaiseFltDivideByZeroException(THREADID tid, ADDRINT exceptionCode)
{
    PIN_TryStart(tid, AnalysisHandler1, 0);

    {
        PIN_TryStart(tid, AnalysisHandler2, 0);

        PIN_TryEnd(tid);
    }

    std::cout << "Raise exception with code " << exceptionCode << std::endl;

    volatile float zero = 0.0;
    volatile float i    = 1.0;
    i /= zero;
    (void)i;

    PIN_TryEnd(tid);

    return exceptionCode;
}

/*!
 * INS analysis routines.
 */
VOID OnIns1()
{
    // Generate exception in inlined analysis routine.
    isGlobalExpected2 = TRUE;
    *ptr2             = 11111;
}

VOID OnIns2()
{
    // Generate exception in inlined analysis routine.
    isGlobalExpected2 = TRUE;
    if (mprotect((void*)ptr2, 1024, PROT_READ))
    {
        perror("Couldn't mprotect");
        exit(-1);
    }
    PIN_LockClient();
    *ptr2 = 22222;
    PIN_UnlockClient();
}

//=================================================================================================
/*!
 * RTN instrumentation routine.
 */
static VOID InstrumentRoutine(RTN rtn, VOID*)
{
    if (PIN_UndecorateSymbolName(RTN_Name(rtn), UNDECORATION_NAME_ONLY) == "RaiseIntDivideByZeroException")
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(ADDRINT), CALLINGSTD_DEFAULT, "RaiseIntDivideByZeroException", PIN_PARG(ADDRINT),
                                     PIN_PARG(ADDRINT), PIN_PARG_END());
        RTN_ReplaceSignature(rtn, AFUNPTR(OnRaiseIntDivideByZeroException), IARG_PROTOTYPE, proto, IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_END);
    }
    else if (PIN_UndecorateSymbolName(RTN_Name(rtn), UNDECORATION_NAME_ONLY) == "RaiseFltDivideByZeroException")
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(void), CALLINGSTD_DEFAULT, "RaiseFltDivideByZeroException", PIN_PARG(ADDRINT),
                                     PIN_PARG_END());
        RTN_ReplaceSignature(rtn, AFUNPTR(OnRaiseFltDivideByZeroException), IARG_PROTOTYPE, proto, IARG_THREAD_ID,
                             IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    }
}

/*!
 * INS instrumentation routine.
 */
VOID Instruction(INS ins, VOID* v)
{
    static bool instrumented[2] = {false, false};
    static ADDRINT ins_addresses[2];
    ADDRINT ins_address = INS_Address(ins);

    if (!instrumented[0])
    {
        instrumented[0]  = true;
        ins_addresses[0] = ins_address;
    }
    else if (!instrumented[1] && (ins_addresses[0] != ins_address))
    {
        instrumented[1]  = true;
        ins_addresses[1] = ins_address;
    }

    if (ins_addresses[0] == ins_address)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)OnIns1, IARG_END);
    }
    else if (ins_addresses[1] == ins_address)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)OnIns2, IARG_END);
    }
}

//=================================================================================================
/*!
 * Start function
 */
VOID AppStart(VOID* v)
{
    isGlobalExpected1 = TRUE;
    //Will generate an exception
    ADDRINT* addressPair[2];
    addressPair[0] = 0;
    addressPair[1] = (ADDRINT*)malloc(sizeof(ADDRINT));

    ToolRaiseAccessInvalidAddressException(addressPair, 0x12345);
}

//=================================================================================================
/*!
 * Fini function
 */

VOID Fini(INT32 code, VOID* v)
{
    isGlobalExpected3 = TRUE;
    //Will generate an exception
    int j = *(int*)ptr1;
    if (j != 0)
    {
        std::cout << "internal_exception_handler : Unexpected value: " << j << std::endl;
    }
    else
    {
        std::cout << "internal_exception_handler : Completed successfully" << std::endl;
    }
}

//=================================================================================================
/*!
 * The main procedure of the tool.
 */
int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    PIN_AddInternalExceptionHandler(GlobalHandler1, NULL);
    PIN_AddInternalExceptionHandler(GlobalHandler2, NULL);
    RTN_AddInstrumentFunction(InstrumentRoutine, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddApplicationStartFunction(AppStart, 0);
    PIN_AddFiniFunction(Fini, 0);

    //Reserve memory
    void* p = mmap(0, 1024, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0); //  (ADDRINT*)(((long)p + PAGESIZE - 1) & ~(PAGESIZE - 1));

    if (MAP_FAILED == p)
    {
        perror("mmap failed");
        exit(-1);
    }

    ptr1 = (ADDRINT*)p;

    p = mmap(0, 1024, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0); //  (ADDRINT*)(((long)p + PAGESIZE - 1) & ~(PAGESIZE - 1));

    if (MAP_FAILED == p)
    {
        perror("mmap failed");
        exit(-1);
    }

    ptr2 = (ADDRINT*)p;

    ADDRINT ip         = reinterpret_cast< ADDRINT >(&ToolIpAccessInvalidAddressException);
    ADDRINT catchPoint = reinterpret_cast< ADDRINT >(&ToolCatchAccessInvalidAddressException);
    ip2catchMap[ip]    = catchPoint;

    ip              = reinterpret_cast< ADDRINT >(&ToolIpIntDivideByZeroException);
    catchPoint      = reinterpret_cast< ADDRINT >(&ToolCatchIntDivideByZeroException);
    ip2catchMap[ip] = catchPoint;

    PIN_StartProgram();
    return 0;
}
