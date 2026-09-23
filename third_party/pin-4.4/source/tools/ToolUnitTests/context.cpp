/*
 * Copyright (C) 2006-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <string.h>






KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "context.out", "specify output file name");

std::ofstream out;

ADDRINT BeforeIP = 0x0;

int failure = 0;

THREADID myThread = INVALID_THREADID;

ADDRINT IfMyThread(THREADID threadId) { return threadId == myThread; }

#if defined(TARGET_IA32) || defined(TARGET_IA32E)

VOID PrintContext(const CONTEXT* ctxt)
{
    out << "ip:    " << PIN_GetContextReg(ctxt, REG_INST_PTR) << std::endl;
    out << "gax:   " << PIN_GetContextReg(ctxt, REG_GAX) << std::endl;
    out << "gbx:   " << PIN_GetContextReg(ctxt, REG_GBX) << std::endl;
    out << "gcx:   " << PIN_GetContextReg(ctxt, REG_GCX) << std::endl;
    out << "gdx:   " << PIN_GetContextReg(ctxt, REG_GDX) << std::endl;
    out << "gsi:   " << PIN_GetContextReg(ctxt, REG_GSI) << std::endl;
    out << "gdi:   " << PIN_GetContextReg(ctxt, REG_GDI) << std::endl;
    out << "gbp:   " << PIN_GetContextReg(ctxt, REG_GBP) << std::endl;
    out << "sp:    " << PIN_GetContextReg(ctxt, LEVEL_BASE::REG_ESP) << std::endl;

    out << "ss:    " << PIN_GetContextReg(ctxt, REG_SEG_SS) << std::endl;
    out << "cs:    " << PIN_GetContextReg(ctxt, REG_SEG_CS) << std::endl;
    out << "ds:    " << PIN_GetContextReg(ctxt, REG_SEG_DS) << std::endl;
    out << "es:    " << PIN_GetContextReg(ctxt, REG_SEG_ES) << std::endl;
    out << "fs:    " << PIN_GetContextReg(ctxt, REG_SEG_FS) << std::endl;
    out << "gs:    " << PIN_GetContextReg(ctxt, REG_SEG_GS) << std::endl;
    out << "gflags:" << PIN_GetContextReg(ctxt, REG_GFLAGS) << std::endl;
    out << std::endl;
}

VOID CheckFpState(CONTEXT* ctxt)
{
    FPSTATE fpState;
    FPSTATE fpStateCopy;
    // need to clear the fp state as Get/Set FP state will not copy over all the state
    // e.g. in case we are running on machine without AVX/AVX512 support
    memset((void *)(&fpState), 0, FPSTATE_SIZE);
    memset((void *)(&fpStateCopy), 0, FPSTATE_SIZE);
    PIN_GetContextFPState(ctxt, &fpState);
    PIN_SetContextFPState(ctxt, &fpState);
    PIN_GetContextFPState(ctxt, &fpStateCopy);
    if (memcmp(&fpState, &fpStateCopy, FPSTATE_SIZE) != 0)
    {
        failure++;
        out << "Failure!! Mismatch in FP state" << std::endl;
    }
}

#else

VOID PrintContext(const CONTEXT* ctxt) {}
VOID CheckFpState(CONTEXT* ctxt) {}

#endif

VOID SetBeforeContext(CONTEXT* ctxt)
{
    BeforeIP = (ADDRINT)PIN_GetContextReg(ctxt, REG_INST_PTR);
    CheckFpState(ctxt);
}

VOID ShowAfterContext(const CONTEXT* ctxt)
{
    ADDRINT AfterIP = (ADDRINT)PIN_GetContextReg(ctxt, REG_INST_PTR);

    if (BeforeIP == AfterIP)
    {
        failure++;
        out << "Failure!! Before Context IP = " << BeforeIP << ", After Context IP = " << AfterIP << std::endl;
    }
    else
    {
        //        out << "Success!! Before Context IP = " << BeforeIP << ", After Context IP = " << AfterIP << std::endl;
    }
}

VOID ShowTakenBrContext(const CONTEXT* ctxt)
{
    ADDRINT TakenIP = (ADDRINT)PIN_GetContextReg(ctxt, REG_INST_PTR);

    if (BeforeIP == TakenIP)
    {
        failure++;
        out << "Failure!! Before Context IP = " << BeforeIP << ", Branch Taken Context IP = " << TakenIP << std::endl;
    }
    else
    {
        //        out << "Success!! Before Context IP = " << BeforeIP << ", Branch Taken Context IP = " << TakenIP << std::endl;
    }
}

VOID ShowTakenIndirBrContext(const CONTEXT* ctxt)
{
    ADDRINT TakenIP = (ADDRINT)PIN_GetContextReg(ctxt, REG_INST_PTR);

    if (BeforeIP == TakenIP)
    {
        failure++;
        out << "Failure!! Before Context IP = " << BeforeIP << ", Indirect Branch Taken Context IP = "
            << "unknown" << std::endl;
    }
    else
    {
        //        out << "Success!! Before Context IP = " << BeforeIP << ", Indirect Branch Taken Context IP = " << "unknown" << std::endl;
    }
}

VOID Instruction(INS ins, VOID* v)
{
    if (INS_IsControlFlow(ins))
    {
        INS_InsertIfCall(ins, IPOINT_BEFORE, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, AFUNPTR(SetBeforeContext), IARG_CONTEXT, IARG_END);

        if (INS_IsBranch(ins) && INS_IsValidForIpointAfter(ins))
        {
            INS_InsertIfCall(ins, IPOINT_AFTER, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
            INS_InsertThenCall(ins, IPOINT_AFTER, AFUNPTR(ShowAfterContext), IARG_CONTEXT, IARG_END);
        }

        if (INS_IsDirectControlFlow(ins) && INS_IsValidForIpointTakenBranch(ins))
        {
            INS_InsertIfCall(ins, IPOINT_TAKEN_BRANCH, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
            INS_InsertThenCall(ins, IPOINT_TAKEN_BRANCH, AFUNPTR(ShowTakenBrContext), IARG_CONTEXT, IARG_END);
        }

        if (INS_IsIndirectControlFlow(ins) && INS_IsValidForIpointTakenBranch(ins))
        {
            INS_InsertIfCall(ins, IPOINT_TAKEN_BRANCH, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
            INS_InsertThenCall(ins, IPOINT_TAKEN_BRANCH, AFUNPTR(ShowTakenIndirBrContext), IARG_CONTEXT, IARG_END);
        }
    }
}

VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    if (myThread == INVALID_THREADID)
    {
        myThread = threadid;
    }
}

VOID Fini(INT32 code, VOID* v)
{
    if (failure > 0)
        out << "Test detected " << failure << " failures." << std::endl;
    else
        out << "Success!! Test passed!" << std::endl;
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    out.open(KnobOutputFile.Value().c_str());
    out << std::hex;
    out.setf(std::ios::showbase);

    PIN_AddThreadStartFunction(ThreadStart, NULL);
    INS_AddInstrumentFunction(Instruction, NULL);
    PIN_AddFiniFunction(Fini, NULL);

    // Never returns
    PIN_StartProgram();

    return 0;
}
