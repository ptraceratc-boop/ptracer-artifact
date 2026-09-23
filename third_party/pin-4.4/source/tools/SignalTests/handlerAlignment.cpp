/*
 * Copyright (C) 2013-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// This application verifies that Pin sets up the signal handler stack with proper alignment on IA32.
// The compiler expects "esp+4" to be 16-byte aligned on entry to the signal handler function.
// We use a movaps instruction, which requires 16-byte alignment, to test for proper stack alignment.

#include <cassert>

#include <signal.h>
#include <errno.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <unistd.h>
#include <setjmp.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#ifndef _SC_MINSIGSTKSZ // For supporting old libc versions that don't have _SC_MINSIGSTKSZ defined.
#define _SC_MINSIGSTKSZ 249
#endif
/////////////////////
// TYPE DEFINITIONS
/////////////////////

enum TestResult
{
    TR_SUCCESS = 0,
    TR_INVALID_ARG_NUM,
    TR_INVALID_ARGUMENT,
    TR_SIGALTSTACK_FAILED,
    TR_SIGACTION_FAILED,
    TR_ACTION_INSTEADOF_HANDLER,
    TR_HANDLER_INSTEADOF_ACTION,
    TR_NOHANDLER_EXPECTED_HANDLER,
    TR_NOHANDLER_EXPECTED_ACTION,
    TR_UNEXPECTED_SETJMP_VALUE,
    TR_UNKNOWN_ERROR
};

/////////////////////
// GLOBAL VARIABLES
/////////////////////

unsigned long resumeIp = 0;
jmp_buf jumpBuffer;
stack_t theAltStack;
bool simpleHandlerCalled = false;
bool actionHandlerCalled = false;

std::string resultStrings[] = {"Test completed successfully",
                               "Invalid number of arguments",
                               "Invalid argument: ",
                               "Failed to set up an alternate signal stack (sigaltstack failed)",
                               "Failed to set up the signal handler (sigaction failed)",
                               "Expected SimpleHandler to be used as the signal handler, but ActionHandler was called instead",
                               "Expected ActionHandler to be used as the signal handler, but SimpleHandler was called instead",
                               "Both SimpleHandler and ActionHandler were not called, expected SimpleHandler",
                               "Both SimpleHandler and ActionHandler were not called, expected ActionHandler",
                               "Received unexpected value from setjmp: ",
                               "Unexpected error encountered"};

/////////////////////
// UTILITY FUNCTIONS
/////////////////////

extern "C" void TestAlignment();

void DoSegv() __attribute__((noinline));

// This function raises a SEGV exception by attempting to access the address 0x0.
// It also sets the resumeIp variable with a recovery address.
void DoSegv()
{
    //    std::cout << "In DoSegv" << std::endl; // for debug
    asm volatile("movl $resume, %0;"
                 "mov  $0, %%eax;"
                 "mov  %%eax, (%%eax);"
                 "resume:"
                 : "=m"(resumeIp)::"%eax");
}

static void SimpleHandler(int sig)
{
    //    std::cout << "In SimpleHandler" << std::endl; // for debug
    TestAlignment();
    simpleHandlerCalled = true;
    longjmp(jumpBuffer, 1);
}

static void ActionHandler(int sig, siginfo_t* info, void* uctxt)
{
    //    std::cout << "In ActionHandler" << std::endl; // for debug
    TestAlignment();
    actionHandlerCalled              = true;
    ucontext_t* ctxt                 = (ucontext_t*)uctxt;
    ctxt->uc_mcontext.gregs[REG_EIP] = resumeIp;
}

static void SysError(TestResult res)
{
    std::string msg = "ERROR:" + resultStrings[res] + "\n";
    perror(msg.c_str());
    exit(res);
}

static void TestError(TestResult res)
{
    std::cerr << "ERROR: " << resultStrings[res] << std::endl;
    exit(res);
}

template< class T > static void TestError(TestResult res, T val)
{
    std::cerr << "ERROR: " << resultStrings[res] << val << std::endl;
    exit(res);
}

static void InstallSignalHandler(bool useSigaction, bool useAltStack)
{
    struct sigaction sigact;
    if (useSigaction)
    {
        sigact.sa_sigaction = ActionHandler;
        sigact.sa_flags     = SA_SIGINFO;
    }
    else
    {
        sigact.sa_handler = SimpleHandler;
        sigact.sa_flags   = 0;
    }

    if (useAltStack)
    {
        sigact.sa_flags |= SA_ONSTACK;

        // Set up the alternate stack.
        auto altStackSize = (size_t)std::max(sysconf(_SC_MINSIGSTKSZ), 0x1000L);
        theAltStack.ss_sp = mmap(NULL, altStackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(theAltStack.ss_sp != MAP_FAILED && "mmap failed for alternate stack");
        theAltStack.ss_size = altStackSize;
        if (sigaltstack(&theAltStack, NULL) != 0) SysError(TR_SIGALTSTACK_FAILED);
    }
    sigemptyset(&sigact.sa_mask);
    if (sigaction(SIGSEGV, &sigact, 0) != 0) SysError(TR_SIGACTION_FAILED);
}

static void DoTest(bool useSigaction, bool useAltStack)
{
    bool success = false;

    // Set up the signal handler.
    InstallSignalHandler(useSigaction, useAltStack);

    // Raise the SEGV exception.
    std::cout << "Raising SEGV" << std::endl;
    int res = setjmp(jumpBuffer);
    if (res == 0)
    {
        // The call to setjmp succeeded, now we can continue with the test.
        // DoSegv (below) returns only if ActionHandler is used. When SimpleHandler is used, we jump back up
        // as if setjmp returned 1.
        DoSegv();
        if (simpleHandlerCalled)
            TestError(TR_HANDLER_INSTEADOF_ACTION);
        else if (actionHandlerCalled)
            success = true;
        else
            TestError(TR_NOHANDLER_EXPECTED_ACTION);
    }
    else if (res == 1)
    {
        // SimpleHandler finished successfully and jumped to this location.
        if (actionHandlerCalled)
            TestError(TR_ACTION_INSTEADOF_HANDLER);
        else if (simpleHandlerCalled)
            success = true;
        else
            TestError(TR_NOHANDLER_EXPECTED_HANDLER);
    }
    else
    {
        // Something weird happened, we shouldn't get here.
        TestError(TR_UNEXPECTED_SETJMP_VALUE, res);
    }
    if (!success) TestError(TR_UNKNOWN_ERROR);
}

/////////////////////
// MAIN FUNCTION
/////////////////////

// Expected arguments:
// [1]: Test scenario
int main(int argc, const char** argv)
{
    if (argc != 2)
    {
        std::cerr << "ERROR: Invalid number of arguments, " << argc - 1 << "." << std::endl;
        std::cerr << "\tUsage:" << std::endl << "\t\thandlerAlignment <Test # (1-4)>" << std::endl;
        return TR_INVALID_ARG_NUM;
    }

    // The DoTest function only returns upon success.
    // In case of an error, an informative message is printed and the application exits.
    if (strcmp(argv[1], "1") == 0)
    {
        std::cout << "Testing SimpleHandler on the regular stack" << std::endl;
        DoTest(/*useSigaction*/ false, /*useAltStack*/ false);
    }
    else if (strcmp(argv[1], "2") == 0)
    {
        std::cout << "Testing ActionHandler on the regular stack" << std::endl;
        DoTest(/*useSigaction*/ true, /*useAltStack*/ false);
    }
    else if (strcmp(argv[1], "3") == 0)
    {
        std::cout << "Testing SimpleHandler on the alternate stack" << std::endl;
        DoTest(/*useSigaction*/ false, /*useAltStack*/ true);
    }
    else if (strcmp(argv[1], "4") == 0)
    {
        std::cout << "Testing ActionHandler on the alternate stack" << std::endl;
        DoTest(/*useSigaction*/ true, /*useAltStack*/ true);
    }
    else
        TestError(TR_INVALID_ARGUMENT, argv[1]);

    std::cout << "Test completed successfully" << std::endl;
    return TR_SUCCESS;
}
