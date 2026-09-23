/*
 * Copyright (C) 2013-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
  * This application allocates memory with different access permissions.
  * The pintool instruments NotifyPinAfterMmap to capture the allocation and verify the accessibility.
  */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#if defined(TARGET_WINDOWS)
#include <cstdio>
#include <cstdlib>
#include <windows.h>
#define EXPORT_SYM extern "C" __declspec(dllexport) __declspec(noinline)
#else
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#define EXPORT_SYM extern "C"
#endif

EXPORT_SYM void NotifyPinAfterMmap(const char* addr, bool is_mapped, bool access_read, bool access_write)
{
    fprintf(stdout, "%s %p read %d write %d mapped %d\n", __FUNCTION__, addr, (int)access_read, (int)access_write,
            (int)is_mapped);
}

#if defined(TARGET_WINDOWS) // WINDOWS

size_t GetPageSize()
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return static_cast< size_t >(sysInfo.dwPageSize);
}

const char* MmapWrapperNoMemoryAccess(size_t size = 0)
{
    if (size == 0)
    {
        size = GetPageSize();
    }
    return reinterpret_cast< const char* >(VirtualAlloc(0, size, MEM_COMMIT, PAGE_NOACCESS));
}

const char* MmapWrapperWriteAndReadMemoryAccess(size_t size = 0)
{
    if (size == 0)
    {
        size = GetPageSize();
    }
    return reinterpret_cast< const char* >(VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE));
}

void MunmapWrapper(const char* addr, size_t size = 0)
{
    VirtualFree(reinterpret_cast< void* >(const_cast< char* >(addr)), size, MEM_RELEASE);
}

#else // LINUX

size_t GetPageSize() { return static_cast< size_t >(getpagesize()); }

const char* MmapWrapperNoMemoryAccess(size_t size = 0)
{
    if (size == 0)
    {
        size = GetPageSize();
    }
    // Use MAP_32BIT to reduce the chance of address reuse by Pin after munmap
    return reinterpret_cast< const char* >(mmap(0, size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0));
}

const char* MmapWrapperWriteAndReadMemoryAccess(size_t size = 0)
{
    if (size == 0)
    {
        size = GetPageSize();
    }
    // Use MAP_32BIT to reduce the chance of address reuse by Pin after munmap
    return reinterpret_cast< const char* >(mmap(0, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_32BIT, -1, 0));
}

void MunmapWrapper(const char* addr, size_t size = 0)
{
    if (size == 0)
    {
        size = GetPageSize();
    }
    munmap(reinterpret_cast< void* >(const_cast< char* >(addr)), size);
}

#endif

int main(int argc, char* argv[])
{
    constexpr bool prot_read  = true;
    constexpr bool prot_write = true;
    constexpr bool is_mapped  = true;

    const char* buffer = MmapWrapperNoMemoryAccess();
    assert(0 != buffer);
    NotifyPinAfterMmap(buffer, is_mapped, !prot_read, !prot_write);

    buffer = MmapWrapperWriteAndReadMemoryAccess();
    assert(0 != buffer);
    NotifyPinAfterMmap(buffer, is_mapped, prot_read, prot_write);

    // We assume that no-one will reuse the address returned by MmapWrapperNoMemoryAccess.
    // after we unmap it and before we check it in the tool. Now this may not be true
    // for "real world" applications, but for the purpose of this test
    // we assume this to be correct.
#if defined(TARGET_WINDOWS)
    const char* bogus_address = MmapWrapperNoMemoryAccess();
    assert(0 != bogus_address);
    MunmapWrapper(
        bogus_address); // Assume noone will reuse this address before we check it in the tool to see it is really unmapped.
#else
    // On Linux, we allocate a larger region and unmap just one page in the middle to reduce the chance that
    // the address is not reused. We notice that on Linux the address is reused very quickly after munmap.
    const char* bogus_address = MmapWrapperWriteAndReadMemoryAccess(3 * GetPageSize());
    assert(0 != bogus_address);
    bogus_address += GetPageSize(); // Move to the second page.
    MunmapWrapper(
        bogus_address,
        GetPageSize()); // Assume noone will reuse this address before we check it in the tool to see it is really unmapped.
#endif
    NotifyPinAfterMmap(reinterpret_cast< const char* >(bogus_address), !is_mapped, !prot_read, !prot_write);

    return 0;
}