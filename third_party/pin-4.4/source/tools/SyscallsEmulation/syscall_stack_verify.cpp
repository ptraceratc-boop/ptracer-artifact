/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This tool verifies that syscall entry and exit callbacks are properly paired
 * and handles nested/stacked syscalls where multiple entry callbacks may occur
 * before the first exit callback.
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stack>

/* ================================================================== */
// Global variables
/* ================================================================== */

// Stack to track syscall numbers for each thread
std::stack< ADDRINT > tSyscallStack[PIN_MAX_THREADS];

// Counters
UINT64 entryCount    = 0;
UINT64 exitCount     = 0;
UINT64 mismatchCount = 0;

std::ostream* out = &std::cerr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for tool output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool verifies that syscall entry and exit callbacks are properly paired." << std::endl
              << "It tracks syscall numbers in a stack to handle nested/stacked syscalls." << std::endl
              << std::endl;

    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

/*!
 * Syscall entry callback - records the syscall number
 */
VOID SyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ADDRINT syscallNum = PIN_GetSyscallNumber(ctxt, std);

#if defined(TARGET_WINDOWS)
    // Skip tracking syscall 165 on Windows - this is NtContinueEx() which does not return,
    // so the exit syscall callback will never be invoked. We skip it to avoid unmatched entries.
    if (syscallNum == 165)
    {
        return;
    }
#endif

    tSyscallStack[threadIndex].push(syscallNum);
    entryCount++;

    *out << "Thread " << threadIndex << ": Entry  - Syscall #" << syscallNum
         << " (stack depth: " << tSyscallStack[threadIndex].size() << ")" << std::endl;
}

/*!
 * Syscall exit callback - verifies the syscall number matches the entry
 */
VOID SyscallExit(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ADDRINT syscallNum = PIN_GetSyscallNumber(ctxt, std);

#if defined(TARGET_WINDOWS)
    // Skip tracking syscall 165 on Windows - this is NtContinueEx() which does not return,
    // so this exit callback should theoretically never be invoked anyway.
    if (syscallNum == 165)
    {
        return;
    }
#endif

    exitCount++;

    if (tSyscallStack[threadIndex].empty())
    {
        *out << "ERROR: Thread " << threadIndex << ": Exit callback for syscall #" << syscallNum << " but stack is empty!"
             << std::endl;
        mismatchCount++;
        return;
    }

    ADDRINT expectedNum = tSyscallStack[threadIndex].top();
    tSyscallStack[threadIndex].pop();

    if (expectedNum != syscallNum)
    {
        *out << "ERROR: Thread " << threadIndex << ": Syscall number mismatch! Expected #" << expectedNum << " but got #"
             << syscallNum << std::endl;
        mismatchCount++;
    }
    else
    {
        *out << "Thread " << threadIndex << ": Exit   - Syscall #" << syscallNum
             << " verified (stack depth: " << tSyscallStack[threadIndex].size() << ")" << std::endl;
    }
}

/*!
 * Thread start callback - ensures stack is empty for new thread
 */
VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    // Clear any existing entries (shouldn't happen, but be safe)
    while (!tSyscallStack[threadid].empty())
    {
        tSyscallStack[threadid].pop();
    }

    *out << "Thread " << threadid << ": Started" << std::endl;
}

/*!
 * Fini callback - print statistics and check for unmatched entries
 */
VOID Fini(INT32 code, VOID* v)
{
    *out << std::endl;
    *out << "=== Syscall Stack Verification Statistics ===" << std::endl;
    *out << "Total entry callbacks:  " << entryCount << std::endl;
    *out << "Total exit callbacks:   " << exitCount << std::endl;
    *out << "Mismatches detected:    " << mismatchCount << std::endl;

    // Check for any remaining unmatched entries
    // Note: Each thread may have exactly one unmatched entry at the end, which is the exit()
    // syscall. The exit callback is not called for the exit() syscall, so this is expected.
    UINT64 unmatchedCount             = 0;
    UINT64 threadsWithOneEntry        = 0;
    UINT64 threadsWithMultipleEntries = 0;

    for (THREADID tid = 0; tid < static_cast< THREADID >(PIN_MAX_THREADS); tid++)
    {
        size_t stackSize = tSyscallStack[tid].size();
        if (stackSize > 0)
        {
            if (stackSize == 1)
            {
                threadsWithOneEntry++;
                *out << "Thread " << tid << " has 1 unmatched entry (likely exit syscall - acceptable)" << std::endl;
            }
            else
            {
                threadsWithMultipleEntries++;
                *out << "ERROR: Thread " << tid << " has " << stackSize << " unmatched syscall entries remaining" << std::endl;
            }
            unmatchedCount += stackSize;
        }
    }

    if (unmatchedCount > 0)
    {
        *out << "Total unmatched entries: " << unmatchedCount << " (threads with 1 entry: " << threadsWithOneEntry
             << ", threads with >1 entries: " << threadsWithMultipleEntries << ")" << std::endl;
    }

    // Success if we have no mismatches and all remaining entries are single entries per thread
    // (which are the expected exit() syscalls without exit callbacks)
    if (mismatchCount == 0 && threadsWithMultipleEntries == 0)
    {
        *out << "SUCCESS: All syscalls properly paired!" << std::endl;
        if (threadsWithOneEntry > 0)
        {
            *out << "Note: " << threadsWithOneEntry << " thread(s) have unmatched exit() syscalls (expected)" << std::endl;
        }
    }
    else
    {
        *out << "FAILURE: Found " << mismatchCount << " mismatches and " << threadsWithMultipleEntries
             << " threads with multiple unmatched entries" << std::endl;
    }

    *out << "=============================================" << std::endl;
}

/*!
 * Main function
 */
int main(int argc, char* argv[])
{
    // Initialize PIN
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Open output file if specified
    std::string fileName = KnobOutputFile.Value();
    if (!fileName.empty())
    {
        out = new std::ofstream(fileName.c_str());
    }

    // Register callbacks
    PIN_AddThreadStartFunction(ThreadStart, NULL);
    PIN_AddSyscallEntryFunction(SyscallEntry, NULL);
    PIN_AddSyscallExitFunction(SyscallExit, NULL);
    PIN_AddFiniFunction(Fini, NULL);

    // Start the program
    PIN_StartProgram();

    return 0;
}
