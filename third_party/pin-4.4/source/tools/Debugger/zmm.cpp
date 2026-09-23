/*
 * Copyright (C) 2017-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <cstring>
#include <cstdint>
#include "tool_macros.h"

typedef uintptr_t PTRINT;

static void HandleSigill(int);

#if defined(TARGET_LINUX)
#define EXPORT_SYM extern "C"
#elif defined(TARGET_WINDOWS)
#define EXPORT_SYM extern "C" __declspec(dllexport)
#else
#error Unknown target OS
#endif
EXPORT_SYM void GlobalFunction();
extern "C" void loadYmm0(const unsigned char*) ASMNAME("loadYmm0");
extern "C" void loadZmm0(const unsigned char*) ASMNAME("loadZmm0");
extern "C" void loadK0(const unsigned char*) ASMNAME("loadK0");

int main(int argc, char* argv[])
{
    if (argc == 2 && std::strcmp(argv[1], "-print-address") == 0)
    {
        PTRINT fp = reinterpret_cast< PTRINT >(GlobalFunction);
        std::cout << "0x" << std::hex << std::noshowbase << fp << "\n";
        return 0;
    }
#if defined(TARGET_LINUX)
    // Create a SIGILL handler in case this processor does not support
    // AVX512 instructions. for Linux
    //
    struct sigaction act;
    act.sa_handler = HandleSigill;
    act.sa_flags   = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGILL, &act, 0);
#endif

    unsigned i;
    unsigned char ymmVal[32];
    unsigned char zmmVal[64];
    unsigned char kVal[8];

    for (i = 0; i < sizeof(ymmVal); i++)
        ymmVal[i] = static_cast< unsigned char >(i + 1);

    for (i = 0; i < sizeof(zmmVal); i++)
        zmmVal[i] = static_cast< unsigned char >(i + 1);

    for (i = 0; i < sizeof(kVal); i++)
        kVal[i] = static_cast< unsigned char >(i + 1);

    // If the processor supports AVX512, the debugger stops at a breakpoint
    // immediately after loading each register.  Otherwise, the debugger stops at
    // a breakpoint in HandleSigill().
    //
    try
    {
        loadK0(kVal);
        loadYmm0(ymmVal);
        loadZmm0(zmmVal);
    }
    catch (...)
    {
        HandleSigill(0);
    }

    GlobalFunction();
    return 0;
}

static void HandleSigill(int sig)
{
    std::cout << "Processor does not support AVX512" << std::endl;
    std::exit(0);
}

extern "C" void GlobalFunction()
{
    /*
     * This is a good place to put Pin instrumentation or to set a breakpoint.
     */
}
