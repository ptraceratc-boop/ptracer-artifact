/*
 * Copyright (C) 2010-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sched.h>

/* Test PIN_SpawnThreadAndExecuteAt in a simple tool.
 * Can be run on any target application, since it only executes a few
 * instructions from there.
 */

static KNOB< BOOL > KnobVerbose(KNOB_MODE_WRITEONCE, "pintool", "v", "0", "Output verbose information");

static KNOB< BOOL > KnobFromCallback(KNOB_MODE_WRITEONCE, "pintool", "c", "0",
                                     "Create threads in callback, rather than analysis functions");

static UINT32 threadsCreated  = 0;
static ADDRINT threadFunction = 0;

// MAXTHREADS includes the static thread that starts the world, so must be >=2 to test anything!
#define MAXTHREADS 2
#define STACKSIZE 1024

static VOID cloneThread(CONTEXT* ctxt)
{
    // Should maybe use atomic increment here, but if we create a few bonus ones, it
    // doesn't really matter!
    ++threadsCreated;

    if (threadsCreated >= MAXTHREADS) return;

    CONTEXT localContext;

    if (!ctxt)
    {
        ctxt = &localContext;
        PIN_SetContextReg(ctxt, REG_GFLAGS, 0);
    }

    if (INVALID_NATIVE_TID == PIN_SpawnApplicationThread(ctxt))
    {
        std::cerr << "PIN_SpawnApplicationThread failed\n";
        PIN_ExitProcess(-1);
    }
    if (KnobVerbose) std::cerr << "Spawned a new thread (" << threadsCreated << ")\n";
}

static VOID ThreadCreateCallback(THREADID tid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    if (KnobVerbose) std::cerr << "Thread create callback for " << tid << "\n";

    // First thread is static, we don't want to mangle it, but we do create a new thread
    // from the callback.
    CONTEXT context;
    if (KnobFromCallback)
    {
        ADDRINT* stack = (ADDRINT*)mmap(0, 1024 * sizeof(ADDRINT), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERTX(MAP_FAILED != stack);

        ctxt = &context;

        if (!threadFunction)
        {
            std::cerr << "Cannot find 'doNothing()' in application\n";
            PIN_ExitProcess(-1);
        }

        CONTEXT newThreadContext;
        PIN_SaveContext(ctxt, &newThreadContext);

        // Fill in sensible values for critical registers.
        PIN_SetContextReg(&newThreadContext, REG_STACK_PTR, ADDRINT(&stack[1023]));
        PIN_SetContextReg(&newThreadContext, REG_INST_PTR, threadFunction);
        PIN_SetContextReg(&newThreadContext, REG_GFLAGS, 0);

        cloneThread(&newThreadContext);
    }

    if (tid >= (MAXTHREADS - 1))
    {
        std::cerr << "Created all threads OK\n";
        PIN_ExitProcess(0);
    }

    // First thread is created statically, we don't want to mess with it.
    if (tid == 0 || KnobFromCallback) return;

    if (!threadFunction)
    {
        std::cerr << "Cannot find 'doNothing()' in application\n";
        PIN_ExitProcess(-1);
    }
}

static VOID ThreadExitCallback(THREADID tid, const CONTEXT* ctxt, INT32 code, VOID* v)
{
    std::cerr << "Thread " << tid << " terminated (" << code << ")\n";
    if (ctxt)
    {
        std::cerr << "Thread " << tid << " IP: " << std::hex << PIN_GetContextReg(ctxt, REG_INST_PTR) << std::dec << "\n";
    }
}

static VOID ContextChangeCallback(THREADID tid, CONTEXT_CHANGE_REASON reason, const CONTEXT* from, CONTEXT* to, INT32 info,
                                  VOID* v)
{
    std::cerr << "Thread " << tid << " context change " << reason << " info " << info << "\n";
    if (from)
    {
        std::cerr << "Thread " << tid << " IP: " << std::hex << PIN_GetContextReg(from, REG_INST_PTR) << std::dec << "\n";
    }
}

static VOID AddInstrumentation(INS ins, VOID*)
{
    static INT InstrumentedInstructions = 0;
    if (InstrumentedInstructions++ < MAXTHREADS)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)cloneThread, IARG_ADDRINT, 0, IARG_END);
    }
}

static VOID ImageLoadCallback(IMG img, VOID* v)
{
    if (threadFunction == 0)
    {
        if (KnobVerbose) std::cerr << "Looking for doNothing in " << IMG_Name(img) << "\n";

        RTN rtn = RTN_FindByName(img, "doNothing");

        if (RTN_Valid(rtn))
        {
            threadFunction = RTN_Address(rtn);
            if (KnobVerbose) std::cerr << "'doNothing' at " << std::hex << threadFunction << std::dec << "\n";
        }
    }
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        std::cerr << "Bad arguments\n";
        return -1;
    }

    PIN_InitSymbols();

    IMG_AddInstrumentFunction(ImageLoadCallback, 0);
    PIN_AddThreadStartFunction(ThreadCreateCallback, 0);
    PIN_AddThreadFiniFunction(ThreadExitCallback, 0);
    PIN_AddContextChangeFunction(ContextChangeCallback, 0);

    if (!KnobFromCallback) INS_AddInstrumentFunction(AddInstrumentation, 0);

    PIN_StartProgram();
}
