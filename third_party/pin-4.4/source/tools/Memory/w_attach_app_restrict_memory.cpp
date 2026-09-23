/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 *
 *
 * Behaviour:
 * - Discover or create address ranges to pass to Pin via -restrict_memory knob.
 * - Start Pin with the -pid <thispid> argument so Pin attaches to this process.
 * - Wait for Pin to attach by polling the `PinAttached()` function (which is
 *   replaced by the test tool when Pin runs) with a timeout.
 *
 * Supported command line switches (same semantics as the Linux app):
 *   -pin <pin-binary>        : full path to Pin executable
 *   -mapped_inside           : create a 3-page area with a mapped middle page and
 *                              pass its full range and the two free subranges.
 *   -none                    : pick an address that misses existing mappings
 *                              and pass it as a single-page restrict_memory range.
 *   -pinarg <...>            : forward following arguments to Pin.
 *
 * Note: the test infrastructure expects the -restrict_memory knob to be passed
 * using numeric (hex) addresses: 0xBASE:0xEND. This program prints ranges in
 * that format.
 */

#include <windows.h>
#include <processthreadsapi.h>
#include <memoryapi.h>
#include <psapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <list>
#include <sstream>
#include <assert.h>

static size_t AllocationGranularity()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<size_t>(si.dwAllocationGranularity);
}

// Format range as 0xADDR:0xEND
static std::string MakeRangeString(void* start, void* end)
{
    std::stringstream ss;
    ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(start) << ":0x" << std::hex << reinterpret_cast<uintptr_t>(end);
    return ss.str();
}

// Create a 3-allocation-granularity restricted range where only the middle
// allocation-granularity block is reserved. The outer blocks are free.
static void CreateRangeWithMappedMiddle(std::string* pinRange, std::string* leftFreeRange, std::string* rightFreeRange)
{
    /*
    1. allocate 64k*3 - make sure i have 64k*3 free space and I get the base address - addr.
    2. release all - All free now.
    3. allocate addr+64k (middle)
    4. give the whole range as input (base is addr and end is addr+3*64k).
    This will also make sure the input is always granularity allocation alinged.
    */
    const size_t allocGran = AllocationGranularity();

    // Temporarily reserve 3 allocation-granularity blocks to find a contiguous
    // allocation-granularity-aligned window that is currently free.
    LPVOID base = VirtualAlloc(NULL, 3 * allocGran, MEM_RESERVE, PAGE_READWRITE);
    assert(base != NULL);

    const uintptr_t baseAddr   = reinterpret_cast< uintptr_t >(base);
    const uintptr_t middleAddr = baseAddr + allocGran;
    const uintptr_t endAddr    = baseAddr + 3 * allocGran;

    assert((baseAddr % allocGran) == 0);
    assert((middleAddr % allocGran) == 0);
    assert((endAddr % allocGran) == 0);

    // Release the whole window. This makes all 3 blocks truly free.
    BOOL released = VirtualFree(base, 0, MEM_RELEASE);
    assert(released);

    // Now reserve only the middle allocation-granularity block at the known address.
    LPVOID middle = VirtualAlloc(reinterpret_cast< void* >(middleAddr),
                                 allocGran,
                                 MEM_RESERVE,
                                 PAGE_READWRITE);
    assert(middle == reinterpret_cast< void* >(middleAddr));

    // Pass a 3-allocation-granularity aligned restrict range that overlaps
    // the single reserved middle block.
    *pinRange = MakeRangeString(reinterpret_cast< void* >(baseAddr),
                                reinterpret_cast< void* >(endAddr));

    *leftFreeRange = MakeRangeString(reinterpret_cast< void* >(baseAddr),
                                     reinterpret_cast< void* >(middleAddr));

    *rightFreeRange = MakeRangeString(reinterpret_cast< void* >(middleAddr + allocGran),
                                      reinterpret_cast< void* >(endAddr));
}

// Walk virtual address space to find the first non-free region base address.
static void* FindMinMappedAddr()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t maxAddr = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi;

    while (addr < maxAddr)
    {
        SIZE_T res = VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi));
        if (res == 0) break;
        if (mbi.State != MEM_FREE)
        {
            return mbi.BaseAddress;
        }
        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        // guard
        if (addr == 0) break;
    }
    return nullptr;
}

extern "C" int PinAttached() { return 0; }

static void PrintArguments(char** inArgv)
{
    fprintf(stderr, "Going to run: ");
    for (unsigned int i = 0; inArgv && inArgv[i] != 0; ++i)
    {
        fprintf(stderr, "%s ", inArgv[i]);
    }
    fprintf(stderr, "\n");
}

void ParseCommandLine(int argc, char* argv[], std::list<std::string>* pinArgs)
{
    std::string pinBinary;
    size_t alloc_gran = AllocationGranularity();
    bool mappedInside = false;
    std::string pinRange, leftFreeRange, rightFreeRange;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-pin")
        {
            if (++i < argc) pinBinary = argv[i];
        }
        else if (arg == "-mapped_inside")
        {
            mappedInside = true;
            CreateRangeWithMappedMiddle(&pinRange, &leftFreeRange, &rightFreeRange);
            pinArgs->push_back("-restrict_memory");
            pinArgs->push_back(pinRange);
        }
        else if (arg == "-none")
        {
            void* min_mapped = FindMinMappedAddr();
            assert(min_mapped);
            uintptr_t noneAddr = reinterpret_cast<uintptr_t>(min_mapped) > (2 * alloc_gran)
                                     ? reinterpret_cast<uintptr_t>(min_mapped) - (2 * alloc_gran)
                                     : reinterpret_cast<uintptr_t>(min_mapped);
            std::stringstream ss;
            ss << "0x" << std::hex << noneAddr << ":0x" << std::hex << (noneAddr + alloc_gran);
            pinArgs->push_back("-restrict_memory");
            pinArgs->push_back(ss.str());
        }
        else if (arg == "-pinarg")
        {
            // forward everything after -pinarg
            for (int parg = ++i; parg < argc; ++parg)
            {
                pinArgs->push_back(argv[parg]);
                ++i;
            }
            break;
        }
    }

    if (mappedInside)
    {
        pinArgs->push_back("-restrict_memory");
        pinArgs->push_back(leftFreeRange);
        pinArgs->push_back("-restrict_memory");
        pinArgs->push_back(rightFreeRange);
    }

    assert(!pinBinary.empty());
    pinArgs->push_front(pinBinary);
}

void StartPin(std::list<std::string>* pinArgs)
{
    auto it = pinArgs->begin();
    std::string exe = *it;
    ++it;

    // Build command line: "<exe>" -pid <thispid> <other args...>
    std::string cmd = "\"" + exe + "\"";

    char pidBuf[64];
    sprintf(pidBuf, " -pid %u", static_cast<unsigned int>(GetCurrentProcessId()));
    cmd += pidBuf;

    for (; it != pinArgs->end(); ++it)
    {
        cmd += " ";
        // quote if contains space
        if (it->find(' ') != std::string::npos)
        {
            cmd += '"' + *it + '"';
        }
        else
        {
            cmd += *it;
        }
    }

    // Create process
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    // For diagnostics print the command line
    fprintf(stderr, "Starting Pin: %s\n", cmd.c_str());

    BOOL ok = CreateProcessA(NULL, const_cast<LPSTR>(cmd.c_str()), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (!ok)
    {
        fprintf(stderr, "CreateProcess failed: %u\n", (unsigned)GetLastError());
        exit(1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char* argv[])
{
    std::list<std::string> pinArgs;
    ParseCommandLine(argc, argv, &pinArgs);
    StartPin(&pinArgs);

    // Wait for Pin to attach (tool replaces PinAttached()) with timeout (20s)
    const int maxLoops = 10; // 10 * 2s = 20s
    int loop = 0;
    fprintf(stderr, "Before pause, waiting on PinAttached\n");
    while (!PinAttached())
    {
        Sleep(2000);
        if (++loop >= maxLoops)
        {
            fprintf(stderr, "Pin is not attached within timeout\n");
            return 1;
        }
    }
    fprintf(stderr, "After pause\n");
    return 0;
}
