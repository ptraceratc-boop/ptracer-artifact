/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>

/*
* This test application verifies that Pin's memory regions are protected
 * from being modified by the application.
 *
 * It first scans the process memory to find where Pin is loaded (by looking
 * for a mapped file whose path contains "pincrt"). Once found, it tries to
 * tamper with that memory region using several low-level Windows NT functions:
 *
 *   - NtAllocateVirtualMemory  : try to allocate memory on top of Pin
 *   - NtProtectVirtualMemory   : try to change Pin's memory permissions
 *   - NtFreeVirtualMemory      : try to free Pin's memory
 *   - NtMapViewOfSection       : try to map a file over Pin's memory
 *   - NtUnmapViewOfSection     : try to unmap Pin's memory
 *
 * Each operation is expected to FAIL. If any of them succeeds, the test
 * fails via an assertion.
 *
 * In other words: this is a negative test that confirms Pin protects
 * its own memory from application interference.
 */

#if defined(_MSC_VER)
#pragma comment(lib, "Psapi.lib")
#endif

namespace
{
constexpr size_t ALLOCATION_SIZE = 4096;
constexpr ULONG VIEW_UNMAP       = 2;

enum class Operation
{
    NtAllocateVirtualMemory,
    NtProtectVirtualMemory,
    NtFreeVirtualMemory,
    NtMapViewOfSection,
    NtUnmapViewOfSection
};

constexpr Operation ALL_OPERATIONS[] = {Operation::NtAllocateVirtualMemory, Operation::NtProtectVirtualMemory,
                                        Operation::NtFreeVirtualMemory, Operation::NtMapViewOfSection,
                                        Operation::NtUnmapViewOfSection};

using NtAllocateVirtualMemory_t = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, ULONG_PTR zeroBits, SIZE_T* regionSize,
                                               ULONG allocationType, ULONG protect);
using NtProtectVirtualMemory_t  = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, SIZE_T* regionSize, ULONG newProtect,
                                              ULONG* oldProtect);
using NtFreeVirtualMemory_t     = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, SIZE_T* regionSize, ULONG freeType);
using NtMapViewOfSection_t      = LONG(NTAPI*)(HANDLE sectionHandle, HANDLE processHandle, PVOID* baseAddress, ULONG_PTR zeroBits,
                                          SIZE_T commitSize, LARGE_INTEGER* sectionOffset, SIZE_T* viewSize,
                                          ULONG inheritDisposition, ULONG allocationType, ULONG win32Protect);
using NtUnmapViewOfSection_t    = LONG(NTAPI*)(HANDLE processHandle, PVOID baseAddress);

bool nt_success(LONG status) { return status >= 0; }

template< typename FunctionType > FunctionType get_ntdll_function(const char* functionName)
{
    auto ntdllModule = GetModuleHandleA("ntdll.dll");
    assert(nullptr != ntdllModule);

    auto functionAddress = GetProcAddress(ntdllModule, functionName);
    assert(nullptr != functionAddress);

    return reinterpret_cast< FunctionType >(functionAddress);
}

bool chars_equal_ignore_case(char lhs, char rhs)
{
    if ('A' <= lhs && lhs <= 'Z')
    {
        lhs = char(lhs - 'A' + 'a');
    }

    if ('A' <= rhs && rhs <= 'Z')
    {
        rhs = char(rhs - 'A' + 'a');
    }

    return lhs == rhs;
}

bool contains_ignore_case(const char* text, const char* needle)
{
    auto textLen   = std::strlen(text);
    auto needleLen = std::strlen(needle);

    if (needleLen == 0 || textLen < needleLen)
    {
        return false;
    }

    for (size_t textOffset = 0; textOffset <= textLen - needleLen; ++textOffset)
    {
        bool match = true;

        for (size_t needleOffset = 0; needleOffset != needleLen; ++needleOffset)
        {
            if (!chars_equal_ignore_case(text[textOffset + needleOffset], needle[needleOffset]))
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            return true;
        }
    }

    return false;
}

bool is_pin_image_path(const char* path) { return contains_ignore_case(path, "pincrt"); }

void* parse_address(const char* addressString)
{
    char* parseEnd = nullptr;

    auto address = static_cast< uintptr_t >(std::strtoull(addressString, &parseEnd, 16));

    if (parseEnd == addressString || *parseEnd != '\0')
    {
        return nullptr;
    }

    return reinterpret_cast< void* >(address);
}

bool parse_operation(const char* operationString, Operation& operation)
{
    if (0 == std::strcmp(operationString, "NtAllocateVirtualMemory"))
    {
        operation = Operation::NtAllocateVirtualMemory;
    }
    else if (0 == std::strcmp(operationString, "NtProtectVirtualMemory"))
    {
        operation = Operation::NtProtectVirtualMemory;
    }
    else if (0 == std::strcmp(operationString, "NtFreeVirtualMemory"))
    {
        operation = Operation::NtFreeVirtualMemory;
    }
    else if (0 == std::strcmp(operationString, "NtMapViewOfSection"))
    {
        operation = Operation::NtMapViewOfSection;
    }
    else if (0 == std::strcmp(operationString, "NtUnmapViewOfSection"))
    {
        operation = Operation::NtUnmapViewOfSection;
    }
    else
    {
        return false;
    }
    return true;
}

void print_usage(const char* programName)
{
    std::cerr << "Usage: " << programName
              << " [-op NtAllocateVirtualMemory|NtProtectVirtualMemory|NtFreeVirtualMemory|NtMapViewOfSection|"
                 "NtUnmapViewOfSection] [-addr <address_in_hex>]"
              << std::endl;
}

void* find_pin_mapping()
{
    SYSTEM_INFO systemInfo {};
    GetSystemInfo(&systemInfo);

    auto currentAddress = reinterpret_cast< uintptr_t >(systemInfo.lpMinimumApplicationAddress);
    auto maximumAddress = reinterpret_cast< uintptr_t >(systemInfo.lpMaximumApplicationAddress);

    while (currentAddress < maximumAddress)
    {
        MEMORY_BASIC_INFORMATION memoryInfo {};

        auto querySize = VirtualQuery(reinterpret_cast< LPCVOID >(currentAddress), &memoryInfo, sizeof(memoryInfo));

        if (0 == querySize)
        {
            break;
        }

        if (MEM_FREE != memoryInfo.State && nullptr != memoryInfo.AllocationBase)
        {
            char mappedFileName[MAX_PATH * 4] {};

            auto fileNameLength = GetMappedFileNameA(GetCurrentProcess(), memoryInfo.BaseAddress, mappedFileName,
                                                     static_cast< DWORD >(sizeof(mappedFileName)));

            if (0 != fileNameLength && is_pin_image_path(mappedFileName))
            {
                return memoryInfo.AllocationBase;
            }
        }

        auto regionEnd = reinterpret_cast< uintptr_t >(memoryInfo.BaseAddress) + memoryInfo.RegionSize;
        currentAddress = (regionEnd > currentAddress) ? regionEnd : currentAddress + systemInfo.dwPageSize;
    }

    return nullptr;
}

void overlap_with_nt_allocate_virtual_memory(void* addr)
{
    auto ntAllocateVirtualMemory = get_ntdll_function< NtAllocateVirtualMemory_t >("NtAllocateVirtualMemory");

    PVOID baseAddress = addr;
    SIZE_T regionSize = ALLOCATION_SIZE;
    auto status =
        ntAllocateVirtualMemory(GetCurrentProcess(), &baseAddress, 0, &regionSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    assert(!nt_success(status));
}

void overlap_with_nt_protect_virtual_memory(void* addr)
{
    auto ntProtectVirtualMemory = get_ntdll_function< NtProtectVirtualMemory_t >("NtProtectVirtualMemory");

    PVOID baseAddress = addr;
    SIZE_T regionSize = ALLOCATION_SIZE;
    ULONG oldProtect  = 0;
    auto status       = ntProtectVirtualMemory(GetCurrentProcess(), &baseAddress, &regionSize, PAGE_READWRITE, &oldProtect);

    assert(!nt_success(status));
}

void overlap_with_nt_free_virtual_memory(void* addr)
{
    auto ntFreeVirtualMemory = get_ntdll_function< NtFreeVirtualMemory_t >("NtFreeVirtualMemory");

    PVOID baseAddress = addr;
    SIZE_T regionSize = 0;
    auto status       = ntFreeVirtualMemory(GetCurrentProcess(), &baseAddress, &regionSize, MEM_RELEASE);

    assert(!nt_success(status));
}

void overlap_with_nt_map_view_of_section(void* addr)
{
    auto ntMapViewOfSection = get_ntdll_function< NtMapViewOfSection_t >("NtMapViewOfSection");

    HANDLE section =
        CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast< DWORD >(ALLOCATION_SIZE), nullptr);
    assert(nullptr != section);

    PVOID baseAddress = addr;
    SIZE_T viewSize   = ALLOCATION_SIZE;
    auto status =
        ntMapViewOfSection(section, GetCurrentProcess(), &baseAddress, 0, 0, nullptr, &viewSize, VIEW_UNMAP, 0, PAGE_READWRITE);

    assert(!nt_success(status));

    auto closeResult = CloseHandle(section);
    assert(FALSE != closeResult);
}

void overlap_with_nt_unmap_view_of_section(void* addr)
{
    auto ntUnmapViewOfSection = get_ntdll_function< NtUnmapViewOfSection_t >("NtUnmapViewOfSection");

    auto status = ntUnmapViewOfSection(GetCurrentProcess(), addr);

    assert(!nt_success(status));
}

void run_operation(Operation operation, void* addr)
{
    switch (operation)
    {
        case Operation::NtAllocateVirtualMemory:
            overlap_with_nt_allocate_virtual_memory(addr);
            break;
        case Operation::NtProtectVirtualMemory:
            overlap_with_nt_protect_virtual_memory(addr);
            break;
        case Operation::NtFreeVirtualMemory:
            overlap_with_nt_free_virtual_memory(addr);
            break;
        case Operation::NtMapViewOfSection:
            overlap_with_nt_map_view_of_section(addr);
            break;
        case Operation::NtUnmapViewOfSection:
            overlap_with_nt_unmap_view_of_section(addr);
            break;
    }
}

void run_all_operations(void* addr)
{
    for (auto operation : ALL_OPERATIONS)
    {
        run_operation(operation, addr);
    }
}

} // namespace

int main(int argc, char* argv[])
{
    auto addr = find_pin_mapping();
    if (nullptr == addr)
    {
        std::cerr << "Failed to find a Pin mapping." << std::endl;
        return 1;
    }
    run_all_operations(addr);

    return 0;
}
