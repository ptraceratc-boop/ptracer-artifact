/*
 * Copyright (C) 2012-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Pin runs this application without a Pintool. The test exercises Pin's reconstruction of the application register context
 * when an instruction that uses EBX implicitly raises an exception.
 *
 * TestAccessViolations() initializes the application registers with sentinel values and deliberately faults on cmpxchg8b [edx],
 * xlat, and cmpxchg8b [ebx]. The signal handler verifies each fault site and resumes at the matching assembly label.
 * Assembly then checks that Pin preserved the application register values. The test succeeds after all three faults are handled
 * in order.
 */

#define NEED_UCONTEXT_T
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

extern "C" unsigned char* GetResumeIp(unsigned char* ip, unsigned int exceptionNumber, const char** instruction);
extern "C" int TestAccessViolations();
int numExceptions = 0;

void PrintSignalContext(int sig, const siginfo_t* info, void* vctxt)
{
    ucontext_t* ctxt = (ucontext_t*)vctxt;
    unsigned long rip;
    long int trapno;

#if defined(TARGET_LINUX) && defined(TARGET_IA32E)
    rip    = (unsigned long)ctxt->uc_mcontext.gregs[REG_RIP];
    trapno = (long int)ctxt->uc_mcontext.gregs[REG_TRAPNO];
#elif defined(TARGET_LINUX) && defined(TARGET_IA32)
    rip    = (unsigned long)ctxt->uc_mcontext.gregs[REG_EIP];
    trapno = (long int)ctxt->uc_mcontext.gregs[REG_TRAPNO];
#endif

    fprintf(stderr, "  PrintSignal: sig %d, pc=0x%lx, si_errno=%d, trap_no=%ld", sig, rip, (int)info->si_errno, trapno);

    fprintf(stderr, "\n");
}

static void Handle(int sig, siginfo_t* info, void* v)
{
    fprintf(stderr, "Handle\n");
    fflush(stderr);

    ucontext_t* ctxt = (ucontext_t*)v;

    PrintSignalContext(sig, info, v);
    numExceptions++;

    unsigned char* faultIp = (unsigned char*)
#if defined(TARGET_LINUX) && defined(TARGET_IA32)
                                 ctxt->uc_mcontext.gregs[REG_EIP];
#elif defined(TARGET_LINUX) && defined(TARGET_IA32E)
                                 ctxt->uc_mcontext.gregs[REG_RIP];
#else
#error "Undefined code"
#endif
    const char* instruction;
    unsigned char* resumeIp = GetResumeIp(faultIp, numExceptions, &instruction);
    if (resumeIp == 0)
    {
        fprintf(stderr, "***Error unexpected exception at ip %p\n", faultIp);
        exit(1);
    }
    fprintf(stderr, "segv at: %s\n", instruction);
    fprintf(stderr, " setting resume ip to %p\n", resumeIp);
#if defined(TARGET_LINUX) && defined(TARGET_IA32)
    ctxt->uc_mcontext.gregs[REG_EIP] =
#elif defined(TARGET_LINUX) && defined(TARGET_IA32E)
    ctxt->uc_mcontext.gregs[REG_RIP] =
#else
#error "Undefined code"
#endif
        (unsigned long)(resumeIp);
}

int main(int argc, char** argv)
{
    struct sigaction sigact;

    sigact.sa_sigaction = Handle;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sigact, 0) == -1)
    {
        fprintf(stderr, "Unable handle SIGSEGV\n");
        return 1;
    }
    if (sigaction(SIGBUS, &sigact, 0) == -1)
    {
        fprintf(stderr, "Unable handle SIGBUS\n");
        return 1;
    }

    fprintf(stderr, "calling TestAccessViolations\n");
    int retVal = TestAccessViolations();

    if (!retVal || numExceptions != 3)
    {
        fprintf(stderr, "***Error\n");
        exit(1);
    }
}
