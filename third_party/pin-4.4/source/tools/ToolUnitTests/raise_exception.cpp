/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This test tool verifies correctness of the PIN_RaiseException and PIN_SafeCopyEx 
 * API implementation.
 */

#include "pin.H"
#include <string>
#include <iostream>




KNOB< BOOL > KnobUseIargConstContext(KNOB_MODE_WRITEONCE, "pintool", "const_context", "0", "use IARG_CONST_CONTEXT");

/*!
 * RTN analysis routines.
 */
static VOID OnRaiseX87OverflowException(const CONTEXT* ctxt, THREADID tid)
{
    // Raise FP_OVERFLOW exception on behalf of the application
    ADDRINT exceptAddr = PIN_GetContextReg(ctxt, REG_INST_PTR);
    EXCEPTION_INFO exceptInfo;
    PIN_InitExceptionInfo(&exceptInfo, EXCEPTCODE_X87_OVERFLOW, exceptAddr);
    PIN_RaiseException(ctxt, tid, &exceptInfo);
}

static VOID OnRaiseSystemException(const CONTEXT* ctxt, THREADID tid, ADDRINT sysExceptCode)
{
    // Raise FP_OVERFLOW exception on behalf of the application
    ADDRINT exceptAddr = PIN_GetContextReg(ctxt, REG_INST_PTR);
    EXCEPTION_INFO exceptInfo;
    PIN_InitWindowsExceptionInfo(&exceptInfo, static_cast< UINT32 >(sysExceptCode), exceptAddr);
    PIN_RaiseException(ctxt, tid, &exceptInfo);
}

/*!
 * RTN instrumentation routine.
 */
static VOID InstrumentRoutine(RTN rtn, VOID*)
{
    if (PIN_UndecorateSymbolName(RTN_Name(rtn), UNDECORATION_NAME_ONLY) == "RaiseX87OverflowException")
    {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, MAKE_AFUNPTR(OnRaiseX87OverflowException),
                       (KnobUseIargConstContext) ? IARG_CONST_CONTEXT : IARG_CONTEXT, IARG_THREAD_ID, IARG_END);
        RTN_Close(rtn);
    }
    else if (PIN_UndecorateSymbolName(RTN_Name(rtn), UNDECORATION_NAME_ONLY) == "RaiseSystemException")
    {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, MAKE_AFUNPTR(OnRaiseSystemException),
                       (KnobUseIargConstContext) ? IARG_CONST_CONTEXT : IARG_CONTEXT, IARG_THREAD_ID,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_Close(rtn);
    }
}

VOID Fini(INT32 code, VOID* v)
{
    std::cerr << "raise_exception[PIN_SafeCopyEx in Fini]" << std::endl;
    char buf[16];
    EXCEPTION_INFO eceptInfo;

    size_t copySize = PIN_SafeCopyEx(buf, NULL, sizeof(buf), &eceptInfo);
    if (copySize != sizeof(buf))
    {
        EXCEPTION_CODE exceptCode = PIN_GetExceptionCode(&eceptInfo);
        if (PIN_GetExceptionClass(exceptCode) != EXCEPTCLASS_ACCESS_FAULT)
        {
            std::cerr << "Exception class != EXCEPTCLASS_ACCESS_FAULT" << std::endl;
            exit(1);
        }

        FAULTY_ACCESS_TYPE accessType = PIN_GetFaultyAccessType(&eceptInfo);
        if ((accessType != FAULTY_ACCESS_READ) && (accessType != FAULTY_ACCESS_TYPE_UNKNOWN))
        {
            std::cerr << "Unexpected FAULTY_ACCESS_TYPE" << std::endl;
            exit(1);
        }

        ADDRINT accessAddr;
        if (PIN_GetFaultyAccessAddress(&eceptInfo, &accessAddr))
        {
            if (reinterpret_cast< VOID* >(accessAddr) != NULL)
            {
                std::cerr << "Unexpected address of the faulty access" << std::endl;
                exit(1);
            }
        }
    }
    else
    {
        std::cerr << "raise_exception : Unexpected return value of PIN_SafeCopyEx " << std::endl;
    }
    std::cerr << "raise_exception : Completed successfully" << std::endl;
}

/*!
 * The main procedure of the tool.
 */
int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    RTN_AddInstrumentFunction(InstrumentRoutine, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    return 0;
}
