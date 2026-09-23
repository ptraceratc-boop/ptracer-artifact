/*
 * Copyright (C) 2004-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This file contains a test for correctness of the size of memory read
    for cmp instructions with repeat std::string operation prefix.
    (As well as a number of other tests for our handling of REPped std::string
    operations).
 */

#include <iostream>
#include <fstream>
#include <string.h>
#if defined(TARGET_LINUX) || defined(PIN_CRT)
#include <unistd.h>
#endif
#include "pin.H"







#if defined(TARGET_WINDOWS)
#define MAINNAME "main"
#else
#define MAINNAME "_start"
#endif

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

THREADID mainThread = INVALID_THREADID;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

static std::ofstream out;

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "repcmpsz_tool.out", "Output file");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool prints out the size of memory reads done by two cmp \n"
            "instructions with repeat std::string operation prefix.\n"
            "\n";

    std::cerr << KNOB_BASE::StringKnobSummary();

    std::cerr << std::endl;

    return -1;
}

/* ===================================================================== */
VOID printSz(UINT32 sz, ADDRINT addr, UINT32 executing, ADDRINT count)
{
    std::string ex = (executing ? " [TRUE]" : " [FALSE]");
    out << "Read size : " << std::dec << sz << " %ecx " << std::dec << count << ex << std::endl;
}

VOID printCntVal(UINT32 val) { out << "After Count : " << std::dec << val << std::endl; }

VOID printSzPredicated(UINT32 sz, ADDRINT addr, UINT32 executing, ADDRINT count)
{
    out << "Predicated ";
    printSz(sz, addr, executing, count);
}

VOID printCntValPredicated(UINT32 val)
{
    out << "Predicated ";
    printCntVal(val);
}

// Check that IF/THEN is working right, and that multiple AFTER instrumentation works as it should.
static UINT32 icountBits[10];
static UINT32 icount[2];

UINT32 predicate(UINT32 bitNo, ADDRINT ip)
{
    UINT32 ic = icount[0];

    // out << std::hex << ip << " " << std::dec << ic << " Testing bit " << std::dec << bitNo <<
    //     " returns " << ((ic & (1<<bitNo)) != 0) << std::endl;

    return (ic & (1 << bitNo)) != 0;
}

VOID addCount(UINT32 bitNo)
{
    // out << "Adding one for " << std::dec << bitNo << std::endl;
    icountBits[bitNo] += 1;
}

void countInst(UINT32 where)
{
    // out << (where ? "AFTER" : "BEFORE") << " adding one to count" << std::endl;
    icount[where]++;
}

/* ===================================================================== */
#define STRINGIZE(v) #v

/* Information for each thread. */
class ThreadState
{
  public:
    UINT32 iCount;
    CONTEXT context;
};

static ThreadState threadState;

/************************************************************************/

#define REGENTRY(n)                       \
    {                                     \
        LEVEL_BASE::REG_##n, STRINGIZE(n) \
    }

// Table of registers to check and display
static const struct
{
    REG regnum;
    const char* name;
} checkedRegisters[] = {REGENTRY(EFLAGS), REGENTRY(EAX), REGENTRY(EBX), REGENTRY(ECX), REGENTRY(EDX),
                        REGENTRY(EBP),    REGENTRY(ESP), REGENTRY(EDI), REGENTRY(ESI)};

static VOID printRegisterDiffs(THREADID tid, CONTEXT* ctx, UINT32 where)
{
    ThreadState* s    = &threadState;
    UINT32 seqNo      = s->iCount;
    CONTEXT* savedCtx = &s->context;

    // Save the context if this was the first instruction
    if (seqNo == 0)
        PIN_SaveContext(ctx, savedCtx);
    else
    {
        for (UINT32 i = 0; i < sizeof(checkedRegisters) / sizeof(checkedRegisters[0]); i++)
        {
            REG r            = checkedRegisters[i].regnum;
            ADDRINT newValue = PIN_GetContextReg(ctx, r);

            if (PIN_GetContextReg(savedCtx, r) != newValue)
            {
                if (where != 0)
                {
                    out << "*** Instrumentation (" << std::dec << where << ") caused a change ";
                }
                out << std::dec << seqNo << ": " << checkedRegisters[i].name << " = " << std::hex << UINT32(newValue) << std::endl;
                PIN_SetContextReg(savedCtx, r, newValue);
            }
        }
    }
}

/* Check that Pin gets IARG_FIRST_REP_ITERATION right on the very first REPped std::string op.
 * (This tests that the initialization of REG_INST_OUTSIDE_REP in the spill area is
 * correct).
 */
static BOOL firstRepOfAll = TRUE;

static ADDRINT firstTime() { return firstRepOfAll; }

static VOID checkFirstRep(BOOL first)
{
    if (!first)
    {
        out << "*** First REPped instruction had first iteration flag wrong" << std::endl;
        out.close();
        _exit(1);
    }
    else
    {
        out << "FirstREPped instruction had correct first iteration flag" << std::endl;
    }
    firstRepOfAll = FALSE;
}

static UINT32 repStarts     = 0;
static UINT32 repIterations = 0;
static UINT32 repZeros      = 0;

static VOID countReps(BOOL firsttime, BOOL executing)
{
    // Number of first REP iterations
    repStarts += firsttime ? 1 : 0;

    // Number of total REP iterartions
    repIterations += executing ? 1 : 0;

    // Number of REP instructions which had 0 iterations, i.e. count
    // register (RCX/ECX) had 0 in the first iteration.
    repZeros += (!firsttime && !executing) ? 1 : 0;
}

static VOID printCounts()
{
    out << "REP starts     " << repStarts << std::endl;
    out << "REP iterations " << repIterations << std::endl;
    out << "REP zeros      " << repZeros << std::endl;
}

static VOID setMainThreadId(THREADID tid)
{
    ASSERTX(INVALID_THREADID == mainThread);
    mainThread = tid;
}

/* ===================================================================== */
static BOOL instrumenting = FALSE;

VOID Instruction(INS ins, VOID* v)
{
    UINT32 where = 0;

    if ((PIN_ThreadId() != mainThread) && (INVALID_THREADID != mainThread))
    {
        return;
    }

    if (INS_Opcode(ins) == XED_ICLASS_FNOP)
    {
        instrumenting = !instrumenting;
    }

    if (!instrumenting) return;

    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printRegisterDiffs, IARG_THREAD_ID, IARG_CONTEXT, IARG_UINT32, where++, IARG_END);

    // Find std::string ops only.
    if (INS_IsStringop(ins))
    {
        for (UINT32 bit = 0; bit < 5; bit++)
        {
            INS_InsertIfCall(ins, IPOINT_AFTER, (AFUNPTR)predicate, IARG_UINT32, bit, IARG_INST_PTR, IARG_END);
            INS_InsertThenCall(ins, IPOINT_AFTER, (AFUNPTR)addCount, IARG_UINT32, bit, IARG_END);
        }

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printRegisterDiffs, IARG_THREAD_ID, IARG_CONTEXT, IARG_UINT32, where++,
                       IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)countInst, IARG_UINT32, 0, IARG_END);
        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)countInst, IARG_UINT32, 1, IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printRegisterDiffs, IARG_THREAD_ID, IARG_CONTEXT, IARG_UINT32, where++,
                       IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printSz, IARG_MEMORYREAD_SIZE, IARG_INST_PTR, IARG_EXECUTING, IARG_REG_VALUE,
                       LEVEL_BASE::REG_ECX, IARG_END);

        INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)printCntVal, IARG_REG_VALUE, LEVEL_BASE::REG_ECX, IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printRegisterDiffs, IARG_THREAD_ID, IARG_CONTEXT, IARG_UINT32, where++,
                       IARG_END);

        INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)printSzPredicated, IARG_MEMORYREAD_SIZE, IARG_INST_PTR,
                                 IARG_EXECUTING, IARG_REG_VALUE, LEVEL_BASE::REG_ECX, IARG_END);

        INS_InsertPredicatedCall(ins, IPOINT_AFTER, (AFUNPTR)printCntValPredicated, IARG_REG_VALUE, LEVEL_BASE::REG_ECX,
                                 IARG_END);

        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)printRegisterDiffs, IARG_THREAD_ID, IARG_CONTEXT, IARG_UINT32, where++,
                       IARG_END);

        if (INS_HasRealRep(ins))
        {
            INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)firstTime, IARG_END);
            INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)checkFirstRep, IARG_FIRST_REP_ITERATION, IARG_END);

            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)countReps, IARG_FIRST_REP_ITERATION, IARG_EXECUTING, IARG_END);
        }
    }
}

VOID ImageLoad(IMG img, VOID* v)
{
    if (!IMG_IsMainExecutable(img))
    {
        return;
    }

    RTN rtn = RTN_FindByName(img, MAINNAME);
    ASSERTX(RTN_Valid(rtn));

    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)setMainThreadId, IARG_THREAD_ID, IARG_END);
    RTN_Close(rtn);
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID* v)
{
    UINT32 status = 0;
    if (icount[0] != icount[1])
    {
        out << "***Mismatch in total instructions : Before " << std::dec << icount[0] << " After " << std::dec << icount[1] << std::endl;
        status = 1;
    }

    UINT32 expectedCounts[5];
    for (UINT32 i = 0; i < 5; i++)
        expectedCounts[i] = 0;

    for (UINT32 j = 0; j <= icount[0]; j++)
    {
        for (UINT32 i = 0; i < 5; i++)
        {
            if (j & (1 << i))
            {
                expectedCounts[i]++;
            }
        }
    }

    for (UINT32 i = 0; i < 5; i++)
    {
        if (icountBits[i] != expectedCounts[i])
        {
            out << "*** Bit counts failed : " << std::dec << i << " expected " << expectedCounts[i] << " see " << icountBits[i]
                << std::endl;
        }
    }

    printCounts();

    out.close();
    _exit(status * 100 + code);
}

/* ===================================================================== */

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    PIN_InitSymbols();

    std::string filename = KnobOutputFile.Value();

    // Do this before we activate controllers
    out.open(filename.c_str());

    IMG_AddInstrumentFunction(ImageLoad, NULL);

    INS_AddInstrumentFunction(Instruction, NULL);
    PIN_AddFiniFunction(Fini, NULL);

    // Never returns
    PIN_StartProgram();

    return 1;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
