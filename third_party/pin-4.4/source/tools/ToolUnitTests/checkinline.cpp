/*
 * Copyright (C) 2009-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Check that things work OK with inlined instrumentation which is passed
 * IARG_EXECUTING, IARG_FIRST_REP_ITERATION and IARG_BRANCH_TAKEN. 
 * The generation of those IARGs is non-trivial, and I have seen it cause problems...
 *
 * This checks firstly that Pin doesn't assert when we do this,
 * and secondly that we get the same results when we pass the arguments
 * and when we use them in if/then instrumentation.
 */
#include "pin.H"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdlib.h>





static KNOB< std::string > KnobOutput(KNOB_MODE_WRITEONCE, "pintool", "o", "checkinline.out", "output file");

static std::ofstream out;

static UINT64 predicatedTrueCount = 0;
static UINT64 firstRepCount       = 0;

static UINT64 predicatedTrueCountArg = 0;
static UINT64 firstRepCountArg       = 0;

struct branchCount_t
{
    UINT64 taken;
    UINT64 takenArg;
};
static branchCount_t branchCounts[XED_ICLASS_LAST];

THREADID myThread = INVALID_THREADID;

ADDRINT IfMyThread(THREADID threadId) { return threadId == myThread; }

// Trivial analysis routine to pass its argument back in an IfCall so that we can use it
// to control the next piece of instrumentation.
// Simple enough to be inlined.
static ADDRINT returnArg(BOOL arg, THREADID threadId) { return arg && (threadId == myThread); }

static VOID increment(UINT64* counter) { (*counter)++; }

static VOID add(UINT64* counter, BOOL value) { (*counter) += (value ? 1 : 0); }

static VOID InstrumentInstruction(INS ins, VOID*)
{
    if (INS_IsPredicated(ins))
    {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)returnArg, IARG_EXECUTING, IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)increment, IARG_ADDRINT, ADDRINT(&predicatedTrueCount), IARG_END);

        INS_InsertIfCall(ins, IPOINT_BEFORE, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)add, IARG_ADDRINT, ADDRINT(&predicatedTrueCountArg), IARG_EXECUTING,
                           IARG_END);
        // CountOp = TRUE;
    }

    if (INS_HasRealRep(ins))
    {
        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)returnArg, IARG_FIRST_REP_ITERATION, IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)increment, IARG_ADDRINT, ADDRINT(&firstRepCount), IARG_END);

        INS_InsertIfCall(ins, IPOINT_BEFORE, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)add, IARG_ADDRINT, ADDRINT(&firstRepCountArg), IARG_FIRST_REP_ITERATION,
                           IARG_END);
    }

    if (INS_IsBranch(ins))
    {
        UINT32 op = INS_Opcode(ins);

        INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)returnArg, IARG_BRANCH_TAKEN, IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)increment, IARG_ADDRINT, ADDRINT(&(branchCounts[op].taken)), IARG_END);

        INS_InsertIfCall(ins, IPOINT_BEFORE, AFUNPTR(IfMyThread), IARG_THREAD_ID, IARG_END);
        INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)add, IARG_ADDRINT, ADDRINT(&(branchCounts[op].takenArg)),
                           IARG_BRANCH_TAKEN, IARG_END);
    }
}

static VOID Fini(INT32, VOID*)
{
    out << "                          If/Then              Arg" << std::endl;
    out << "True predicated: " << std::setw(16) << predicatedTrueCount << " " << std::setw(16) << predicatedTrueCountArg << std::endl;
    out << "First Rep      : " << std::setw(16) << firstRepCount << " " << std::setw(16) << firstRepCountArg << std::endl;
    UINT64 takenTotal    = 0;
    UINT64 takenArgTotal = 0;
    for (UINT32 i = 0; i < (sizeof(branchCounts) / sizeof(branchCounts[0])); i++)
    {
        takenTotal    = takenTotal + branchCounts[i].taken;
        takenArgTotal = takenArgTotal + branchCounts[i].takenArg;
    }
    out << "Branch taken   : " << std::setw(16) << takenTotal << " " << std::setw(16) << takenArgTotal << std::endl;
    out << std::endl;

    for (UINT32 i = 0; i < (sizeof(branchCounts) / sizeof(branchCounts[0])); i++)
    {
        UINT64 taken    = branchCounts[i].taken;
        UINT64 takenArg = branchCounts[i].takenArg;

        if (taken || takenArg)
        {
            std::string mnemonic = OPCODE_StringShort(i);

            out << std::setw(20) << std::left << mnemonic << std::right << std::setw(13) << taken << " " << std::setw(16) << takenArg;
            if (taken != takenArg) out << " <==ERROR==";
            out << std::endl;
        }
    }

    if (predicatedTrueCount != predicatedTrueCountArg || firstRepCount != firstRepCountArg || takenTotal != takenArgTotal)
    {
        out << "***MISMATCH***" << std::endl;
        exit(1);
    }
    out.close();
    exit(0);
}

static VOID CheckThreadCount(THREADID threadIndex, CONTEXT*, INT32, VOID*)
{
    if (myThread == INVALID_THREADID)
    {
        myThread = threadIndex;
    }

#ifndef _WIN32
    ASSERT(threadIndex == myThread, "This tool does not handle multiple threads\n");
#endif
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    out.open(KnobOutput.Value().c_str());

    INS_AddInstrumentFunction(InstrumentInstruction, 0);

    // Fini prints the results.
    PIN_AddFiniFunction(Fini, 0);
    PIN_AddThreadStartFunction(CheckThreadCount, 0);

    // Never returns
    PIN_StartProgram();

    return 0;
}
