/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// <COMPONENT>: PinTools
// <FILE-TYPE>: implementation

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/*
 * This test application verifies that Pin correctly handles memory operations
 * on address ranges that Pin has marked as "restricted" (reserved for its own use).
 *
 * The test receives a memory range from the command line (as start:end in hex)
 * and then tries various Windows low-level memory operations on that range,
 * checking that each operation either succeeds or fails with the exact expected
 * error code.
 *
 * The operations tested are:
 *   - NtAllocateVirtualMemory    : reserve and commit memory inside the range
 *   - NtFreeVirtualMemory        : decommit and release memory inside the range
 *   - NtProtectVirtualMemory     : change memory protection inside the range
 *   - NtMapViewOfSection         : map a file/section into the range
 *   - NtUnmapViewOfSection       : unmap a section from the range
 *   - NtAllocateVirtualMemoryEx  : same as above but using the newer Ex variant
 *   - NtMapViewOfSectionEx       : same as above but using the newer Ex variant
 *   - NtUnmapViewOfSectionEx     : same as above but using the newer Ex variant
 *
 * The test walks through a sequence of states for each memory slot
 * (free -> reserved -> committed -> decommitted -> released) and verifies
 * that Pin intercepts each call correctly and returns the right result.
 *
*/

namespace
{
constexpr LONG STATUS_CONFLICTING_ADDRESSES_VALUE = static_cast< LONG >(0xC0000018UL);
constexpr LONG STATUS_INVALID_PARAMETER_VALUE     = static_cast< LONG >(0xC000000DUL);
constexpr LONG STATUS_MEMORY_NOT_ALLOCATED_VALUE  = static_cast< LONG >(0xC00000A0UL);
constexpr LONG STATUS_RESERVED_PROTECT_VALUE      = static_cast< LONG >(0xC000002DUL);
constexpr LONG STATUS_UNABLE_TO_FREE_VM_VALUE     = static_cast< LONG >(0xC000001AUL);
constexpr ULONG VIEW_UNMAP                        = 2;
constexpr SIZE_T REQUIRED_GRANULARITY_SLOTS       = 4;

struct MemoryRange
{
    uintptr_t start; ///< Inclusive start of the restricted range passed by the makefile.
    uintptr_t end;   ///< Exclusive end of the restricted range passed by the makefile.
};

using NtAllocateVirtualMemory_t = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, ULONG_PTR zeroBits, SIZE_T* regionSize,
                                               ULONG allocationType, ULONG protect);
using NtFreeVirtualMemory_t     = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, SIZE_T* regionSize, ULONG freeType);
using NtProtectVirtualMemory_t  = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, SIZE_T* regionSize, ULONG newProtect,
                                              ULONG* oldProtect);
using NtMapViewOfSection_t      = LONG(NTAPI*)(HANDLE sectionHandle, HANDLE processHandle, PVOID* baseAddress, ULONG_PTR zeroBits,
                                          SIZE_T commitSize, LARGE_INTEGER* sectionOffset, SIZE_T* viewSize,
                                          ULONG inheritDisposition, ULONG allocationType, ULONG win32Protect);
using NtUnmapViewOfSection_t    = LONG(NTAPI*)(HANDLE processHandle, PVOID baseAddress);
using NtAllocateVirtualMemoryEx_t = LONG(NTAPI*)(HANDLE processHandle, PVOID* baseAddress, SIZE_T* regionSize,
                                                 ULONG allocationType, ULONG pageProtection, PVOID extendedParameters,
                                                 ULONG extendedParameterCount);
using NtMapViewOfSectionEx_t      = LONG(NTAPI*)(HANDLE sectionHandle, HANDLE processHandle, PVOID* baseAddress,
                                            LARGE_INTEGER* sectionOffset, SIZE_T* viewSize, ULONG allocationType,
                                            ULONG pageProtection, PVOID extendedParameters, ULONG extendedParameterCount);
using NtUnmapViewOfSectionEx_t    = LONG(NTAPI*)(HANDLE processHandle, PVOID baseAddress, ULONG flags);

/**
 * @brief NTDLL entry points used by the restricted-memory semantics test.
 */
struct NtFunctions
{
    NtAllocateVirtualMemory_t allocateVirtualMemory;
    NtFreeVirtualMemory_t freeVirtualMemory;
    NtProtectVirtualMemory_t protectVirtualMemory;
    NtMapViewOfSection_t mapViewOfSection;
    NtUnmapViewOfSection_t unmapViewOfSection;
    NtAllocateVirtualMemoryEx_t allocateVirtualMemoryEx;
    NtMapViewOfSectionEx_t mapViewOfSectionEx;
    NtUnmapViewOfSectionEx_t unmapViewOfSectionEx;
};

/**
 * @brief Checks the success bit of an NTSTATUS-style return value.
 *
 * @param[in] status Status returned by an Nt* function.
 * @return True if the status indicates success.
 */
bool nt_success(LONG status) { return status >= 0; }

/**
 * @brief Prints a test failure and exits with a non-zero status.
 *
 * @param[in] message Human-readable failure text.
 */
[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "restrict_memory_semantics: %s\n", message);
    std::exit(1);
}

/**
 * @brief Load an NTDLL function used by the test app.
 *
 * @tparam FunctionType Function pointer type expected by the caller.
 * @param[in] functionName Export name to load from ntdll.dll.
 * @return Typed function pointer.
 */
template< typename FunctionType > FunctionType get_ntdll_function(const char* functionName)
{
    auto ntdllModule = GetModuleHandleA("ntdll.dll");
    if (nullptr == ntdllModule)
    {
        fail("GetModuleHandleA(ntdll.dll) failed");
    }

    auto functionAddress = GetProcAddress(ntdllModule, functionName);
    if (nullptr == functionAddress)
    {
        fail("GetProcAddress failed");
    }

    return reinterpret_cast< FunctionType >(functionAddress);
}

/**
 * @brief Try to load an optional NTDLL function used only on Windows versions that export it.
 *
 * @tparam FunctionType Function pointer type expected by the caller.
 * @param[in] functionName Export name to load from ntdll.dll.
 * @return Typed function pointer, or nullptr when the export is unavailable.
 */
template< typename FunctionType > FunctionType get_optional_ntdll_function(const char* functionName)
{
    auto ntdllModule = GetModuleHandleA("ntdll.dll");
    if (nullptr == ntdllModule)
    {
        fail("GetModuleHandleA(ntdll.dll) failed");
    }

    return reinterpret_cast< FunctionType >(GetProcAddress(ntdllModule, functionName));
}

/**
 * @brief Load every NTDLL entry point once for the test run.
 *
 * @return Function pointer table used by the individual test flows.
 */
NtFunctions load_nt_functions()
{
    NtFunctions functions {};
    functions.allocateVirtualMemory = get_ntdll_function< NtAllocateVirtualMemory_t >("NtAllocateVirtualMemory");
    functions.freeVirtualMemory       = get_ntdll_function< NtFreeVirtualMemory_t >("NtFreeVirtualMemory");
    functions.protectVirtualMemory    = get_ntdll_function< NtProtectVirtualMemory_t >("NtProtectVirtualMemory");
    functions.mapViewOfSection        = get_ntdll_function< NtMapViewOfSection_t >("NtMapViewOfSection");
    functions.unmapViewOfSection      = get_ntdll_function< NtUnmapViewOfSection_t >("NtUnmapViewOfSection");
    functions.allocateVirtualMemoryEx = get_optional_ntdll_function< NtAllocateVirtualMemoryEx_t >("NtAllocateVirtualMemoryEx");
    functions.mapViewOfSectionEx      = get_optional_ntdll_function< NtMapViewOfSectionEx_t >("NtMapViewOfSectionEx");
    functions.unmapViewOfSectionEx    = get_optional_ntdll_function< NtUnmapViewOfSectionEx_t >("NtUnmapViewOfSectionEx");
    return functions;
}

/**
 * @brief Check whether all optional Ex NTDLL memory syscalls are available for this run.
 *
 * @param[in] functions Loaded NTDLL entry points.
 * @return True if every Ex syscall tested by this app is exported.
 */
bool ex_syscalls_supported(const NtFunctions& functions)
{
    return nullptr != functions.allocateVirtualMemoryEx && nullptr != functions.mapViewOfSectionEx &&
           nullptr != functions.unmapViewOfSectionEx;
}

/**
 * @brief Parse one hexadecimal endpoint from the makefile-provided restricted range argument.
 *
 * @param[in,out] text Cursor into the range string; advanced past the parsed endpoint.
 * @return Parsed endpoint value.
 */
uintptr_t parse_hex_component(const char*& text)
{
    char* parseEnd = nullptr;
    auto value     = static_cast< uintptr_t >(std::strtoull(text, &parseEnd, 16));
    if (parseEnd == text)
    {
        fail("failed to parse range component");
    }
    text = parseEnd;
    return value;
}

/**
 * @brief Parse a half-open base:end range.
 *
 * @param[in] rangeText Range argument passed by the makefile.
 * @return Parsed memory range.
 */
MemoryRange parse_range(const char* rangeText)
{
    const char* cursor = rangeText;
    auto start         = parse_hex_component(cursor);
    if (*cursor != ':')
    {
        fail("range is missing ':' separator");
    }
    ++cursor;
    auto end = parse_hex_component(cursor);
    if (*cursor != '\0' || start >= end)
    {
        fail("range has invalid boundaries");
    }
    return {start, end};
}

/**
 * @brief Assert that an operation returned an exact expected failure status.
 *
 * @param[in] status         Actual status.
 * @param[in] expectedStatus Expected status.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_status(LONG status, LONG expectedStatus, const char* operation)
{
    if (status != expectedStatus)
    {
        std::fprintf(stderr, "restrict_memory_semantics: %s returned 0x%08lx, expected 0x%08lx\n", operation, status,
                     expectedStatus);
        std::exit(1);
    }
}

/**
 * @brief Assert that an NTSTATUS-style operation succeeded.
 *
 * @param[in] status    Actual status.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_success(LONG status, const char* operation)
{
    if (!nt_success(status))
    {
        std::fprintf(stderr, "restrict_memory_semantics: %s failed with 0x%08lx\n", operation, status);
        std::exit(1);
    }
}

/**
 * @brief Assert that an operation returned the requested restricted base address.
 *
 * @param[in] actual    Actual base address.
 * @param[in] expected  Expected base address.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_base(void* actual, uintptr_t expected, const char* operation)
{
    if (actual != reinterpret_cast< void* >(expected))
    {
        std::fprintf(stderr, "restrict_memory_semantics: %s returned base %p, expected %p\n", operation, actual,
                     reinterpret_cast< void* >(expected));
        std::exit(1);
    }
}

/**
 * @brief Assert that an operation returned the expected native-rounded region size.
 *
 * @param[in] actual    Actual region size.
 * @param[in] expected  Expected region size.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_region_size(SIZE_T actual, SIZE_T expected, const char* operation)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "restrict_memory_semantics: %s returned size 0x%zx, expected 0x%zx\n", operation,
                     static_cast< size_t >(actual), static_cast< size_t >(expected));
        std::exit(1);
    }
}

/**
 * @brief Compute the base address of one allocation-granularity slot inside the restricted range.
 *
 * @param[in] range                 Restricted range supplied to Pin.
 * @param[in] allocationGranularity Windows allocation granularity.
 * @param[in] slotIndex             Zero-based slot index.
 * @return Slot base address.
 */
uintptr_t granularity_slot(const MemoryRange& range, SIZE_T allocationGranularity, SIZE_T slotIndex)
{
    return range.start + static_cast< uintptr_t >(slotIndex * allocationGranularity);
}

/**
 * @brief Call NtAllocateVirtualMemory and return its final base/size outputs to the caller.
 *
 * @param[in]  functions      Loaded NTDLL entry points.
 * @param[in]  base           Requested base address.
 * @param[in]  size           Requested region size.
 * @param[in]  allocationType Allocation type passed to NtAllocateVirtualMemory.
 * @param[in]  protect        Protection passed to NtAllocateVirtualMemory.
 * @param[out] finalBase      Final BaseAddress output.
 * @param[out] finalSize      Final RegionSize output.
 * @return NTSTATUS returned by NtAllocateVirtualMemory.
 */
LONG allocate_at(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect, PVOID& finalBase,
                 SIZE_T& finalSize)
{
    finalBase = reinterpret_cast< PVOID >(base);
    finalSize = size;
    return functions.allocateVirtualMemory(GetCurrentProcess(), &finalBase, 0, &finalSize, allocationType, protect);
}

/**
 * @brief Call NtAllocateVirtualMemoryEx and return its final base/size outputs to the caller.
 *
 * @param[in]  functions      Loaded NTDLL entry points.
 * @param[in]  base           Requested base address.
 * @param[in]  size           Requested region size.
 * @param[in]  allocationType Allocation type passed to NtAllocateVirtualMemoryEx.
 * @param[in]  protect        Protection passed to NtAllocateVirtualMemoryEx.
 * @param[out] finalBase      Final BaseAddress output.
 * @param[out] finalSize      Final RegionSize output.
 * @return NTSTATUS returned by NtAllocateVirtualMemoryEx.
 */
LONG allocate_ex_at(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect,
                    PVOID& finalBase, SIZE_T& finalSize)
{
    finalBase = reinterpret_cast< PVOID >(base);
    finalSize = size;
    return functions.allocateVirtualMemoryEx(GetCurrentProcess(), &finalBase, &finalSize, allocationType, protect, nullptr, 0);
}

/**
 * @brief Require a successful NtAllocateVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions      Loaded NTDLL entry points.
 * @param[in] base           Requested base address.
 * @param[in] size           Requested region size.
 * @param[in] allocationType Allocation type passed to NtAllocateVirtualMemory.
 * @param[in] protect        Protection passed to NtAllocateVirtualMemory.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_allocate_success(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect,
                              const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = allocate_at(functions, base, size, allocationType, protect, baseAddress, regionSize);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(regionSize, size, operation);
}

/**
 * @brief Require a failed NtAllocateVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions      Loaded NTDLL entry points.
 * @param[in] base           Requested base address.
 * @param[in] size           Requested region size.
 * @param[in] allocationType Allocation type passed to NtAllocateVirtualMemory.
 * @param[in] protect        Protection passed to NtAllocateVirtualMemory.
 * @param[in] expectedStatus Expected NTSTATUS.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_allocate_status(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect,
                             LONG expectedStatus, const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = allocate_at(functions, base, size, allocationType, protect, baseAddress, regionSize);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Require a successful NtAllocateVirtualMemoryEx operation at an exact restricted slot.
 */
void require_allocate_ex_success(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect,
                                 const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = allocate_ex_at(functions, base, size, allocationType, protect, baseAddress, regionSize);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(regionSize, size, operation);
}

/**
 * @brief Require a failed NtAllocateVirtualMemoryEx operation at an exact restricted slot.
 */
void require_allocate_ex_status(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG allocationType, ULONG protect,
                                LONG expectedStatus, const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = allocate_ex_at(functions, base, size, allocationType, protect, baseAddress, regionSize);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Call NtFreeVirtualMemory and return its final base/size outputs to the caller.
 *
 * @param[in]  functions Loaded NTDLL entry points.
 * @param[in]  base      Requested base address.
 * @param[in]  size      Requested region size.
 * @param[in]  freeType  FreeType passed to NtFreeVirtualMemory.
 * @param[out] finalBase Final BaseAddress output.
 * @param[out] finalSize Final RegionSize output.
 * @return NTSTATUS returned by NtFreeVirtualMemory.
 */
LONG free_at(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG freeType, PVOID& finalBase, SIZE_T& finalSize)
{
    finalBase = reinterpret_cast< PVOID >(base);
    finalSize = size;
    return functions.freeVirtualMemory(GetCurrentProcess(), &finalBase, &finalSize, freeType);
}

/**
 * @brief Require a successful NtFreeVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions Loaded NTDLL entry points.
 * @param[in] base      Requested base address.
 * @param[in] size      Requested region size.
 * @param[in] freeType  FreeType passed to NtFreeVirtualMemory.
 * @param[in] finalSize Expected final RegionSize output.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_free_success(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG freeType, SIZE_T finalSize,
                          const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = free_at(functions, base, size, freeType, baseAddress, regionSize);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(regionSize, finalSize, operation);
}

/**
 * @brief Require a failed NtFreeVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions      Loaded NTDLL entry points.
 * @param[in] base           Requested base address.
 * @param[in] size           Requested region size.
 * @param[in] freeType       FreeType passed to NtFreeVirtualMemory.
 * @param[in] expectedStatus Expected NTSTATUS.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_free_status(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG freeType, LONG expectedStatus,
                         const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    auto status       = free_at(functions, base, size, freeType, baseAddress, regionSize);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Call NtProtectVirtualMemory and return the native status.
 *
 * @param[in]  functions  Loaded NTDLL entry points.
 * @param[in]  base       Requested base address.
 * @param[in]  size       Requested region size.
 * @param[in]  newProtect New protection passed to NtProtectVirtualMemory.
 * @param[out] finalBase  Final BaseAddress output.
 * @param[out] finalSize  Final RegionSize output.
 * @param[out] oldProtect OldProtect output.
 * @return NTSTATUS returned by NtProtectVirtualMemory.
 */
LONG protect_at(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG newProtect, PVOID& finalBase, SIZE_T& finalSize,
                ULONG& oldProtect)
{
    finalBase  = reinterpret_cast< PVOID >(base);
    finalSize  = size;
    oldProtect = 0;
    return functions.protectVirtualMemory(GetCurrentProcess(), &finalBase, &finalSize, newProtect, &oldProtect);
}

/**
 * @brief Require a successful NtProtectVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions          Loaded NTDLL entry points.
 * @param[in] base               Requested base address.
 * @param[in] size               Requested region size.
 * @param[in] newProtect         New protection passed to NtProtectVirtualMemory.
 * @param[in] expectedOldProtect Expected previous protection.
 * @param[in] operation          Operation name used in diagnostics.
 */
void require_protect_success(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG newProtect,
                             ULONG expectedOldProtect, const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    ULONG oldProtect  = 0;
    auto status       = protect_at(functions, base, size, newProtect, baseAddress, regionSize, oldProtect);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(regionSize, size, operation);
    if (oldProtect != expectedOldProtect)
    {
        std::fprintf(stderr, "restrict_memory_semantics: %s old protect 0x%08lx, expected 0x%08lx\n", operation, oldProtect,
                     expectedOldProtect);
        std::exit(1);
    }
}

/**
 * @brief Require a failed NtProtectVirtualMemory operation at an exact restricted slot.
 *
 * @param[in] functions      Loaded NTDLL entry points.
 * @param[in] base           Requested base address.
 * @param[in] size           Requested region size.
 * @param[in] newProtect     New protection passed to NtProtectVirtualMemory.
 * @param[in] expectedStatus Expected NTSTATUS.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_protect_status(const NtFunctions& functions, uintptr_t base, SIZE_T size, ULONG newProtect, LONG expectedStatus,
                            const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T regionSize = 0;
    ULONG oldProtect  = 0;
    auto status       = protect_at(functions, base, size, newProtect, baseAddress, regionSize, oldProtect);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Create a page-file-backed section used by the restricted section mapping tests.
 *
 * @param[in] size Section size in bytes.
 * @return Valid section handle owned by the caller.
 */
HANDLE create_test_section(SIZE_T size)
{
    auto section = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast< DWORD >(size), nullptr);
    if (nullptr == section)
    {
        fail("CreateFileMappingA failed");
    }
    return section;
}

/**
 * @brief Call NtMapViewOfSection at an exact restricted slot.
 *
 * @param[in]  functions Loaded NTDLL entry points.
 * @param[in]  section   Section handle to map.
 * @param[in]  base      Requested base address.
 * @param[in]  size      Requested view size.
 * @param[out] finalBase Final BaseAddress output.
 * @param[out] finalSize Final ViewSize output.
 * @return NTSTATUS returned by NtMapViewOfSection.
 */
LONG map_view_at(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, PVOID& finalBase, SIZE_T& finalSize)
{
    finalBase = reinterpret_cast< PVOID >(base);
    finalSize = size;
    return functions.mapViewOfSection(section, GetCurrentProcess(), &finalBase, 0, 0, nullptr, &finalSize, VIEW_UNMAP, 0,
                                      PAGE_READWRITE);
}

/**
 * @brief Call NtMapViewOfSectionEx at an exact restricted slot.
 *
 * @param[in]  functions Loaded NTDLL entry points.
 * @param[in]  section   Section handle to map.
 * @param[in]  base      Requested base address.
 * @param[in]  size      Requested view size.
 * @param[out] finalBase Final BaseAddress output.
 * @param[out] finalSize Final ViewSize output.
 * @return NTSTATUS returned by NtMapViewOfSectionEx.
 */
LONG map_view_ex_at(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, PVOID& finalBase,
                    SIZE_T& finalSize)
{
    finalBase = reinterpret_cast< PVOID >(base);
    finalSize = size;
    return functions.mapViewOfSectionEx(section, GetCurrentProcess(), &finalBase, nullptr, &finalSize, 0, PAGE_READWRITE, nullptr,
                                        0);
}

/**
 * @brief Require a successful NtMapViewOfSection operation at an exact restricted slot.
 *
 * @param[in] functions Loaded NTDLL entry points.
 * @param[in] section   Section handle to map.
 * @param[in] base      Requested base address.
 * @param[in] size      Requested view size.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_map_view_success(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T viewSize   = 0;
    auto status       = map_view_at(functions, section, base, size, baseAddress, viewSize);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(viewSize, size, operation);
}

/**
 * @brief Require a failed NtMapViewOfSection operation at an exact restricted slot.
 *
 * @param[in] functions      Loaded NTDLL entry points.
 * @param[in] section        Section handle to map.
 * @param[in] base           Requested base address.
 * @param[in] size           Requested view size.
 * @param[in] expectedStatus Expected NTSTATUS.
 * @param[in] operation      Operation name used in diagnostics.
 */
void require_map_view_status(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, LONG expectedStatus,
                             const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T viewSize   = 0;
    auto status       = map_view_at(functions, section, base, size, baseAddress, viewSize);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Require a successful NtMapViewOfSectionEx operation at an exact restricted slot.
 */
void require_map_view_ex_success(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T viewSize   = 0;
    auto status       = map_view_ex_at(functions, section, base, size, baseAddress, viewSize);
    require_success(status, operation);
    require_base(baseAddress, base, operation);
    require_region_size(viewSize, size, operation);
}

/**
 * @brief Require a failed NtMapViewOfSectionEx operation at an exact restricted slot.
 */
void require_map_view_ex_status(const NtFunctions& functions, HANDLE section, uintptr_t base, SIZE_T size, LONG expectedStatus,
                                const char* operation)
{
    PVOID baseAddress = nullptr;
    SIZE_T viewSize   = 0;
    auto status       = map_view_ex_at(functions, section, base, size, baseAddress, viewSize);
    require_status(status, expectedStatus, operation);
}

/**
 * @brief Require a successful NtUnmapViewOfSection operation at an exact restricted slot.
 *
 * @param[in] functions Loaded NTDLL entry points.
 * @param[in] base      Base address to unmap.
 * @param[in] operation Operation name used in diagnostics.
 */
void require_unmap_view_success(const NtFunctions& functions, uintptr_t base, const char* operation)
{
    auto status = functions.unmapViewOfSection(GetCurrentProcess(), reinterpret_cast< PVOID >(base));
    require_success(status, operation);
}

/**
 * @brief Require a successful NtUnmapViewOfSectionEx operation at an exact restricted slot.
 */
void require_unmap_view_ex_success(const NtFunctions& functions, uintptr_t base, const char* operation)
{
    auto status = functions.unmapViewOfSectionEx(GetCurrentProcess(), reinterpret_cast< PVOID >(base), 0);
    require_success(status, operation);
}

/**
 * @brief Validate restricted placeholder transitions for private reserve/commit/protect/free operations.
 *
 * @param[in] functions             Loaded NTDLL entry points.
 * @param[in] range                 Restricted range supplied to Pin.
 * @param[in] allocationGranularity Windows allocation granularity.
 */
void test_private_reserve_commit_flow(const NtFunctions& functions, const MemoryRange& range, SIZE_T allocationGranularity)
{
    const auto reservedThenCommittedSlot = granularity_slot(range, allocationGranularity, 0);
    const auto reserveCommitSlot         = granularity_slot(range, allocationGranularity, 1);
    const auto mixedReservedSlot         = granularity_slot(range, allocationGranularity, 2);
    const auto mixedFreeSlot             = granularity_slot(range, allocationGranularity, 3);

    // Commit without a prior application reservation must still look like native commit-to-free-address behavior.
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(commit restricted-free)");

    // A request with neither MEM_RESERVE nor MEM_COMMIT exercises the restricted-free invalid-parameter path.
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, 0, PAGE_READWRITE,
                            STATUS_INVALID_PARAMETER_VALUE, "NtAllocateVirtualMemory(no allocation type restricted-free)");

    // Reserve replaces the Pin-created placeholder with an application-owned reservation.
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                             "NtAllocateVirtualMemory(reserve restricted-free)");

    // Reserving an already app-reserved restricted slot must fail before the native call.
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve restricted-reserved)");
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve+commit restricted-reserved)");

    // Commit should now succeed because the address is application-reserved restricted memory.
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                             "NtAllocateVirtualMemory(commit restricted-reserved)");

    // Committing an already committed restricted slot follows native Windows behavior and succeeds.
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                             "NtAllocateVirtualMemory(commit restricted-committed)");

    // Reserving any already committed restricted slot must fail before the native call.
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve restricted-committed)");
    require_allocate_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve+commit restricted-committed)");

    // Reserve+commit on a fresh restricted-free slot exercises the combined two-step path.
    require_allocate_success(functions, reserveCommitSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                             "NtAllocateVirtualMemory(reserve+commit restricted-free)");
    require_allocate_success(functions, reserveCommitSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                             "NtAllocateVirtualMemory(commit reserve+commit result)");

    // A mixed app-owned/free span must not be partially committed or partially replaced.
    require_allocate_success(functions, mixedReservedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                             "NtAllocateVirtualMemory(reserve mixed-span first slot)");
    require_allocate_status(functions, mixedReservedSlot, 2 * allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(commit mixed restricted span)");
    require_allocate_status(functions, mixedReservedSlot, 2 * allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve mixed restricted span)");

    // A request that crosses from restricted-free memory into an unmapped gap is invalid and must fail before replacement.
    require_allocate_status(functions, mixedFreeSlot, 2 * allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                            STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemory(reserve restricted/free gap span)");

    // Protect fails on restricted-free and restricted app-reserved memory, and succeeds on restricted app-committed memory.
    require_protect_status(functions, mixedFreeSlot, allocationGranularity, PAGE_READWRITE, STATUS_CONFLICTING_ADDRESSES_VALUE,
                           "NtProtectVirtualMemory(protect restricted-free)");
    require_protect_status(functions, mixedReservedSlot, allocationGranularity, PAGE_READWRITE, STATUS_RESERVED_PROTECT_VALUE,
                           "NtProtectVirtualMemory(protect restricted-reserved)");
    require_protect_success(functions, reservedThenCommittedSlot, allocationGranularity, PAGE_READONLY, PAGE_READWRITE,
                            "NtProtectVirtualMemory(protect restricted-committed)");

    // Decommit returns committed restricted memory to the app-reserved state, and release restores restricted-free.
    require_free_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_DECOMMIT, allocationGranularity,
                         "NtFreeVirtualMemory(decommit restricted-committed)");
    require_protect_status(functions, reservedThenCommittedSlot, allocationGranularity, PAGE_READWRITE,
                           STATUS_RESERVED_PROTECT_VALUE, "NtProtectVirtualMemory(protect decommitted restricted)");
    require_free_success(functions, reservedThenCommittedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(release restricted-reserved)");
    require_free_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_DECOMMIT,
                        STATUS_MEMORY_NOT_ALLOCATED_VALUE, "NtFreeVirtualMemory(decommit restricted-free)");

    // The released placeholder can be reused by the application, proving MEM_RELEASE restored restricted-free state.
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT,
                             PAGE_READWRITE, "NtAllocateVirtualMemory(reserve+commit restored restricted-free)");
    require_free_status(functions, reservedThenCommittedSlot, 0, MEM_DECOMMIT, STATUS_INVALID_PARAMETER_VALUE,
                        "NtFreeVirtualMemory(decommit zero restricted-committed)");
    require_free_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RELEASE | MEM_DECOMMIT,
                        STATUS_INVALID_PARAMETER_VALUE, "NtFreeVirtualMemory(release+decommit restricted-committed)");
    require_free_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RELEASE, STATUS_UNABLE_TO_FREE_VM_VALUE,
                        "NtFreeVirtualMemory(release nonzero restricted-committed)");
    require_free_success(functions, reservedThenCommittedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(release restored restricted-committed)");

    // Section map replaces a restricted-free placeholder; map over app-owned memory fails; unmap restores the placeholder.
    auto section = create_test_section(allocationGranularity);
    require_map_view_success(functions, section, reservedThenCommittedSlot, allocationGranularity,
                             "NtMapViewOfSection(map restricted-free)");
    require_map_view_status(functions, section, reserveCommitSlot, allocationGranularity, STATUS_CONFLICTING_ADDRESSES_VALUE,
                            "NtMapViewOfSection(map restricted-committed)");
    require_unmap_view_success(functions, reservedThenCommittedSlot, "NtUnmapViewOfSection(unmap restricted section)");
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT,
                             PAGE_READWRITE, "NtAllocateVirtualMemory(reserve+commit unmapped section placeholder)");

    require_free_success(functions, reservedThenCommittedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup slot 0)");
    require_free_success(functions, reserveCommitSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup slot 1)");
    require_free_success(functions, mixedReservedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup slot 2)");

    if (!CloseHandle(section))
    {
        fail("CloseHandle(section) failed");
    }
}

/**
 * @brief Validate restricted placeholder transitions through the Windows Ex memory syscalls when they are available.
 *
 * @param[in] functions             Loaded NTDLL entry points.
 * @param[in] range                 Restricted range supplied to Pin.
 * @param[in] allocationGranularity Windows allocation granularity.
 */
void test_ex_memory_flow(const NtFunctions& functions, const MemoryRange& range, SIZE_T allocationGranularity)
{
    if (!ex_syscalls_supported(functions))
    {
        fail("restrict_memory_semantics: NtAllocateVirtualMemoryEx, NtMapViewOfSectionEx, or NtUnmapViewOfSectionEx not exported");
    }

    const auto reservedThenCommittedSlot = granularity_slot(range, allocationGranularity, 0);
    const auto reserveCommitSlot         = granularity_slot(range, allocationGranularity, 1);
    const auto mixedReservedSlot         = granularity_slot(range, allocationGranularity, 2);
    const auto mixedFreeSlot             = granularity_slot(range, allocationGranularity, 3);

    require_allocate_ex_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                               STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemoryEx(commit restricted-free)");
    require_allocate_ex_status(functions, reservedThenCommittedSlot, allocationGranularity, 0, PAGE_READWRITE,
                               STATUS_INVALID_PARAMETER_VALUE, "NtAllocateVirtualMemoryEx(no allocation type restricted-free)");

    require_allocate_ex_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                                "NtAllocateVirtualMemoryEx(reserve restricted-free)");
    require_allocate_ex_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                               STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemoryEx(reserve restricted-reserved)");
    require_allocate_ex_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                                "NtAllocateVirtualMemoryEx(commit restricted-reserved)");
    require_allocate_ex_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                                "NtAllocateVirtualMemoryEx(commit restricted-committed)");
    require_allocate_ex_status(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE, STATUS_CONFLICTING_ADDRESSES_VALUE,
                               "NtAllocateVirtualMemoryEx(reserve+commit restricted-committed)");

    require_allocate_ex_success(functions, reserveCommitSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                                "NtAllocateVirtualMemoryEx(reserve+commit restricted-free)");

    require_allocate_ex_success(functions, mixedReservedSlot, allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                                "NtAllocateVirtualMemoryEx(reserve mixed-span first slot)");
    require_allocate_ex_status(functions, mixedReservedSlot, 2 * allocationGranularity, MEM_COMMIT, PAGE_READWRITE,
                               STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemoryEx(commit mixed restricted span)");
    require_allocate_ex_status(functions, mixedReservedSlot, 2 * allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                               STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemoryEx(reserve mixed restricted span)");
    require_allocate_ex_status(functions, mixedFreeSlot, 2 * allocationGranularity, MEM_RESERVE, PAGE_NOACCESS,
                               STATUS_CONFLICTING_ADDRESSES_VALUE, "NtAllocateVirtualMemoryEx(reserve restricted/free gap span)");

    require_free_success(functions, reservedThenCommittedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup Ex slot 0)");
    require_free_success(functions, reserveCommitSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup Ex slot 1)");
    require_free_success(functions, mixedReservedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup Ex slot 2)");

    auto section = create_test_section(allocationGranularity);
    require_map_view_status(functions, section, reservedThenCommittedSlot, 0, STATUS_CONFLICTING_ADDRESSES_VALUE,
                            "NtMapViewOfSection(zero-size restricted-free)");
    require_map_view_ex_status(functions, section, reservedThenCommittedSlot, 0, STATUS_CONFLICTING_ADDRESSES_VALUE,
                               "NtMapViewOfSectionEx(zero-size restricted-free)");
    require_map_view_ex_success(functions, section, reservedThenCommittedSlot, allocationGranularity,
                                "NtMapViewOfSectionEx(map restricted-free)");
    require_allocate_success(functions, reserveCommitSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
                             "NtAllocateVirtualMemory(reserve+commit map-Ex conflict slot)");
    require_map_view_ex_status(functions, section, reserveCommitSlot, allocationGranularity, STATUS_CONFLICTING_ADDRESSES_VALUE,
                               "NtMapViewOfSectionEx(map restricted-committed)");
    require_free_success(functions, reserveCommitSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup map-Ex conflict slot)");
    require_unmap_view_ex_success(functions, reservedThenCommittedSlot, "NtUnmapViewOfSectionEx(unmap restricted section)");
    require_allocate_success(functions, reservedThenCommittedSlot, allocationGranularity, MEM_RESERVE | MEM_COMMIT,
                             PAGE_READWRITE, "NtAllocateVirtualMemory(reserve+commit Ex-unmapped section placeholder)");
    require_free_success(functions, reservedThenCommittedSlot, 0, MEM_RELEASE, allocationGranularity,
                         "NtFreeVirtualMemory(cleanup Ex section slot)");

    if (!CloseHandle(section))
    {
        fail("CloseHandle(Ex section) failed");
    }
}

} // namespace

/**
 * @brief Entry point for the Windows regular restricted-memory semantics test.
 */
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fail("expected restrict_memory range argument");
    }

    const auto range = parse_range(argv[1]);
    // The Windows implementation requires exact placeholder replacement, so test slots are allocation-granularity sized.
    SYSTEM_INFO systemInfo {};
    GetSystemInfo(&systemInfo);
    const auto allocationGranularity = static_cast< SIZE_T >(systemInfo.dwAllocationGranularity);
    if ((range.end - range.start) < REQUIRED_GRANULARITY_SLOTS * allocationGranularity ||
        0 != (range.start & (allocationGranularity - 1)) || 0 != (range.end & (allocationGranularity - 1)))
    {
        fail("test range must have four allocation-granularity-aligned slots");
    }

    const auto functions = load_nt_functions();
    test_private_reserve_commit_flow(functions, range, allocationGranularity);
    test_ex_memory_flow(functions, range, allocationGranularity);

    std::puts("restrict_memory_semantics: success");
    return 0;
}