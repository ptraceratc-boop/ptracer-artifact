/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 * This application exercises the Windows Ex memory syscalls observed by ex_syscalls_tool.cpp. It resolves the Ex
 * syscall exports from ntdll, allocates writable memory with NtAllocateVirtualMemoryEx, maps an anonymous section twice
 * with NtMapViewOfSectionEx using different protections/view sizes, and unmaps each view with NtUnmapViewOfSectionEx.
 * If the exports or implementation are unavailable, it prints a deterministic skip message used by the makefile test.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS, *PNTSTATUS;

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

typedef NTSTATUS(NTAPI* NtAllocateVirtualMemoryEx_T)(HANDLE processHandle, PVOID* baseAddress, PSIZE_T regionSize,
                                                     ULONG allocationType, ULONG pageProtection,
                                                     PVOID extendedParameters, ULONG extendedParameterCount);
typedef NTSTATUS(NTAPI* NtMapViewOfSectionEx_T)(HANDLE sectionHandle, HANDLE processHandle, PVOID* baseAddress,
                                                PLARGE_INTEGER sectionOffset, PSIZE_T viewSize, ULONG allocationType,
                                                ULONG pageProtection, PVOID extendedParameters,
                                                ULONG extendedParameterCount);
typedef NTSTATUS(NTAPI* NtUnmapViewOfSectionEx_T)(HANDLE processHandle, PVOID baseAddress, ULONG flags);

struct NtdllApis
{
    NtAllocateVirtualMemoryEx_T NtAllocateVirtualMemoryEx;
    NtMapViewOfSectionEx_T NtMapViewOfSectionEx;
    NtUnmapViewOfSectionEx_T NtUnmapViewOfSectionEx;
};

static const NTSTATUS STATUS_UNSUCCESSFUL_LOCAL = static_cast< NTSTATUS >(0xc0000001u);
static const NTSTATUS STATUS_NOT_SUPPORTED_LOCAL = static_cast< NTSTATUS >(0xc00000bbu);
static BOOL sawUnsupported = FALSE;

// Treat STATUS_NOT_SUPPORTED as a skip condition rather than a test failure on platforms without Ex syscall support.
static BOOL IsUnsupportedStatus(NTSTATUS status) { return (status == STATUS_NOT_SUPPORTED_LOCAL); }

// Resolve the Ex syscall entry points dynamically so older Windows versions can skip cleanly.
static BOOL LoadNtdllApis(NtdllApis* apis)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == NULL)
    {
        printf("PASSED: (skipped - Ex syscall exports unavailable)\n");
        return FALSE;
    }

    apis->NtAllocateVirtualMemoryEx =
        reinterpret_cast< NtAllocateVirtualMemoryEx_T >(GetProcAddress(ntdll, "NtAllocateVirtualMemoryEx"));
    apis->NtMapViewOfSectionEx =
        reinterpret_cast< NtMapViewOfSectionEx_T >(GetProcAddress(ntdll, "NtMapViewOfSectionEx"));
    apis->NtUnmapViewOfSectionEx =
        reinterpret_cast< NtUnmapViewOfSectionEx_T >(GetProcAddress(ntdll, "NtUnmapViewOfSectionEx"));

    if ((apis->NtAllocateVirtualMemoryEx == NULL) || (apis->NtMapViewOfSectionEx == NULL) ||
        (apis->NtUnmapViewOfSectionEx == NULL))
    {
        printf("PASSED: (skipped - Ex syscall exports unavailable)\n");
        return FALSE;
    }
    return TRUE;
}

// Return the system page size used for allocation and mapping granularity in this test.
static SIZE_T GetPageSize()
{
    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    return systemInfo.dwPageSize;
}

// Touch the first and last byte of a writable region to prove the mapped/allocated range is accessible.
static BOOL WritePattern(PVOID baseAddress, SIZE_T size)
{
    volatile unsigned char* bytes = static_cast< volatile unsigned char* >(baseAddress);
    bytes[0]                      = 0x5a;
    bytes[size - 1]               = 0xa5;
    return ((bytes[0] == 0x5a) && (bytes[size - 1] == 0xa5));
}

// Read one byte from a read-only view to prove the mapping is accessible without writing to it.
static BOOL ReadMappedByte(PVOID baseAddress)
{
    volatile unsigned char value = *static_cast< volatile unsigned char* >(baseAddress);
    return (value == 0);
}

// Allocate one committed page with NtAllocateVirtualMemoryEx, validate access, then release it with VirtualFree.
static BOOL TestAllocateVirtualMemoryEx(const NtdllApis& apis, SIZE_T pageSize)
{
    PVOID baseAddress = NULL;
    SIZE_T regionSize = pageSize;

    NTSTATUS status = apis.NtAllocateVirtualMemoryEx(GetCurrentProcess(), &baseAddress, &regionSize,
                                                     MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, NULL, 0);

    if (IsUnsupportedStatus(status))
    {
        sawUnsupported = TRUE;
        printf("NtAllocateVirtualMemoryEx reserve_commit status=0x%08lx result=unsupported\n",
               static_cast< unsigned long >(status));
        return TRUE;
    }

    BOOL success = (NT_SUCCESS(status) && (baseAddress != NULL) && (regionSize >= pageSize) &&
                    WritePattern(baseAddress, pageSize));
    if (baseAddress != NULL)
    {
        success = (VirtualFree(baseAddress, 0, MEM_RELEASE) && success);
    }

    printf("NtAllocateVirtualMemoryEx reserve_commit status=0x%08lx result=%s\n", static_cast< unsigned long >(status),
           success ? "ok" : "fail");
    return success;
}

// Create an anonymous section, map it with NtMapViewOfSectionEx, validate access/protection, and unmap it with the Ex API.
static BOOL TestMapViewOfSectionEx(const NtdllApis& apis, const char* testName, DWORD sectionProtect,
                                   ULONG pageProtection, SIZE_T requestedViewSize, BOOL writeAccess, SIZE_T pageSize)
{
    const SIZE_T sectionSize = pageSize * 2;
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, sectionProtect, 0, static_cast< DWORD >(sectionSize),
                                        NULL);
    if (section == NULL)
    {
        printf("%s create_section=fail result=fail\n", testName);
        return FALSE;
    }

    PVOID baseAddress     = NULL;
    SIZE_T viewSize       = requestedViewSize;
    NTSTATUS unmapStatus  = STATUS_UNSUCCESSFUL_LOCAL;
    NTSTATUS mapStatus    = apis.NtMapViewOfSectionEx(section, GetCurrentProcess(), &baseAddress, NULL, &viewSize, 0,
                                                   pageProtection, NULL, 0);
    BOOL memoryAccessOk   = FALSE;

    if (IsUnsupportedStatus(mapStatus))
    {
        CloseHandle(section);
        sawUnsupported = TRUE;
        printf("%s map_status=0x%08lx unmap_status=0x%08lx result=unsupported\n", testName,
               static_cast< unsigned long >(mapStatus), static_cast< unsigned long >(unmapStatus));
        return TRUE;
    }

    if (NT_SUCCESS(mapStatus) && (baseAddress != NULL))
    {
        MEMORY_BASIC_INFORMATION memoryInfo;
        SIZE_T querySize = VirtualQuery(baseAddress, &memoryInfo, sizeof(memoryInfo));
        memoryAccessOk   = ((querySize == sizeof(memoryInfo)) && (memoryInfo.RegionSize >= pageSize));
        memoryAccessOk   = (writeAccess ? (memoryAccessOk && WritePattern(baseAddress, pageSize))
                                         : (memoryAccessOk && ReadMappedByte(baseAddress)));
        unmapStatus      = apis.NtUnmapViewOfSectionEx(GetCurrentProcess(), baseAddress, 0);
    }

    CloseHandle(section);

    BOOL success = (NT_SUCCESS(mapStatus) && NT_SUCCESS(unmapStatus) && memoryAccessOk);
    printf("%s map_status=0x%08lx unmap_status=0x%08lx result=%s\n", testName,
           static_cast< unsigned long >(mapStatus), static_cast< unsigned long >(unmapStatus),
           success ? "ok" : "fail");
    return success;
}

// Run the allocation test and two mapping scenarios: full writable view and one-page read-only view.
int main()
{
    NtdllApis apis;
    if (!LoadNtdllApis(&apis))
    {
        return 0;
    }

    SIZE_T pageSize = GetPageSize();
    BOOL success    = TestAllocateVirtualMemoryEx(apis, pageSize);
    success = (TestMapViewOfSectionEx(apis, "NtMapViewOfSectionEx readwrite_whole_section", PAGE_READWRITE,
                                      PAGE_READWRITE, 0, TRUE, pageSize) && success);
    success = (TestMapViewOfSectionEx(apis, "NtMapViewOfSectionEx readonly_one_page", PAGE_READWRITE, PAGE_READONLY,
                                      pageSize, FALSE, pageSize) && success);

    if (success && sawUnsupported)
    {
        printf("PASSED: (skipped - Ex syscalls not supported)\n");
        return 0;
    }

    printf("%s: Ex syscall app completed\n", success ? "SUCCESS" : "FAILURE");
    return success ? 0 : 1;
}

