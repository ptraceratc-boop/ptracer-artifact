/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

/*
The test app  splits the restricted range into four pages and checks:

1. mprotect on initial restricted-free memory fails with ENOMEM.
2. A non-fixed mmap(hint) into restricted-free memory returns the hinted restricted address.
3. A second non-fixed mmap(hint) to the same page does not reuse it once it is app-owned.
4. MAP_FIXED_NOREPLACE fails with EEXIST when the restricted page is already app-owned.
5. mprotect succeeds on restricted app-owned memory and leaves it in use.
6. After munmap, the restricted page is restored to restricted-free, and mprotect on it fails with ENOMEM.
7. The same hint can be reused after munmap, proving the reservation policy was restored rather than erased.
8. Mapping only the middle page of the restricted range preserves restricted-free neighbors on both sides.
9. MAP_FIXED_NOREPLACE succeeds on a restricted-free page, then fails with EEXIST after that page becomes app-owned.

The optional modes mremap_source_assert and mremap_target_assert intentionally trigger PINOS assertions for unsupported
restricted mremap requests. They are run by dedicated negative make targets.
*/

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace
{
/**
 * @brief Half-open virtual-address range passed to the test by the makefile.
 */
struct MemoryRange
{
    uintptr_t start; ///< Inclusive range start.
    uintptr_t end;   ///< Exclusive range end.
};

enum class TestMode
{
    FullSemantics,
    MremapSourceAssert,
    MremapTargetAssert,
};

/**
 * @brief Prints a diagnostic message and terminates the test process.
 *
 * @param[in] message Human-readable failure description.
 */
[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "restrict_memory_semantics: %s: errno=%d (%s)\n", message, errno, std::strerror(errno));
    std::exit(1);
}

/**
 * @brief Parses the optional test mode argument.
 *
 * @param[in] modeText Optional mode text, or nullptr for the default full semantics run.
 *
 * @return Parsed test mode.
 */
TestMode parse_mode(const char* modeText)
{
    if (nullptr == modeText)
    {
        return TestMode::FullSemantics;
    }
    if (0 == std::strcmp(modeText, "mremap_source_assert"))
    {
        return TestMode::MremapSourceAssert;
    }
    if (0 == std::strcmp(modeText, "mremap_target_assert"))
    {
        return TestMode::MremapTargetAssert;
    }
    fail("unknown test mode");
}

/**
 * @brief Parses one hexadecimal endpoint from the range argument.
 *
 * @param[in, out] text Cursor into the range string. Updated to the first character after the parsed number.
 *
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
 * @brief Parses the restrict_memory range argument in base:end form.
 *
 * @param[in] rangeText Null-terminated range argument passed by the makefile.
 *
 * @return Parsed half-open memory range.
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
 * @brief Issues a non-fixed application mmap with a preferred address hint.
 *
 * @details Under the new PINOS behavior, the first hint into restricted-free memory should return the hinted address even
 *          though the kernel sees a PROT_NONE reservation there.
 *
 * @param[in] hint Preferred mapping address.
 * @param[in] size Mapping size in bytes.
 *
 * @return Native mmap result.
 */
void* map_hint(uintptr_t hint, size_t size)
{
    errno        = 0;
    void* result = mmap(reinterpret_cast< void* >(hint), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (MAP_FAILED == result)
    {
        fail("mmap hint failed");
    }
    return result;
}

/**
 * @brief Issues an application MAP_FIXED_NOREPLACE mmap request.
 *
 * @param[in] address Fixed requested mapping address.
 * @param[in] size Mapping size in bytes.
 *
 * @return Native mmap result, including MAP_FAILED on error.
 */
void* map_noreplace(uintptr_t address, size_t size)
{
    errno        = 0;
    void* result = mmap(reinterpret_cast< void* >(address), size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return result;
}

/**
 * @brief Unmaps an exact application mapping and fails the test on error.
 *
 * @param[in] address Mapping base address.
 * @param[in] size Mapping size in bytes.
 */
void unmap_exact(void* address, size_t size)
{
    if (0 != munmap(address, size))
    {
        fail("munmap failed");
    }
}

/**
 * @brief Verifies that restricted-free memory behaves like unmapped memory for application mprotect.
 *
 * @param[in] address Restricted-free address to protect.
 * @param[in] size Size in bytes to protect.
 */
void expect_mprotect_fails_free(uintptr_t address, size_t size)
{
    errno = 0;
    if (0 == mprotect(reinterpret_cast< void* >(address), size, PROT_READ))
    {
        fail("mprotect unexpectedly succeeded on restricted-free memory");
    }
    if (ENOMEM != errno)
    {
        fail("mprotect on restricted-free memory returned unexpected errno");
    }
}

/**
 * @brief Verifies that application-owned restricted memory follows normal mprotect rules.
 *
 * @param[in] address Application-owned restricted mapping base.
 * @param[in] size Mapping size in bytes.
 */
void expect_mprotect_succeeds(void* address, size_t size)
{
    errno = 0;
    if (0 != mprotect(address, size, PROT_READ))
    {
        fail("mprotect failed on restricted app-owned memory");
    }
    if (0 != mprotect(address, size, PROT_READ | PROT_WRITE))
    {
        fail("mprotect restore failed on restricted app-owned memory");
    }
}

/**
 * @brief Triggers the unsupported restricted-source mremap path.
 *
 * @details The Linux design currently asserts on restricted mremap so QUERY_MEM_RESTRICTED remains tied to the original address
 *          policy. Returning from this call means the assertion did not fire.
 *
 * @param[in] address Restricted app-owned source mapping.
 * @param[in] size Source mapping size in bytes.
 */
void trigger_mremap_restricted_source_assert(void* address, size_t size)
{
    errno = 0;
    (void)mremap(address, size, 2 * size, MREMAP_MAYMOVE);
    fail("mremap restricted source returned instead of triggering a PINOS assert");
}

/**
 * @brief Triggers the unsupported restricted-target mremap path.
 *
 * @param[in] targetAddress Restricted-free target address.
 * @param[in] size Mapping size in bytes.
 */
void trigger_mremap_restricted_target_assert(uintptr_t targetAddress, size_t size)
{
    void* source = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (MAP_FAILED == source)
    {
        fail("mmap source for mremap target test failed");
    }

    errno = 0;
    (void)mremap(source, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, reinterpret_cast< void* >(targetAddress));
    fail("mremap restricted target returned instead of triggering a PINOS assert");
}
} // namespace

/**
 * @brief Exercises Linux application-visible restrict_memory semantics under Pin.
 *
 * @details The test range is divided into four pages. The test verifies hinted mappings into restricted-free memory,
 *          MAP_FIXED_NOREPLACE success and EEXIST behavior, restricted-free restoration after munmap, protection behavior for
 *          restricted-free versus app-owned restricted pages, and split-neighbor preservation. Optional modes intentionally
 *          trigger restricted mremap assertions for the negative make targets.
 *
 * @param[in] argc Argument count. Must be 2.
 * @param[in] argv Argument vector. argv[1] is the restrict_memory range in base:end form.
 *
 * @return Zero on success; fail() exits nonzero on failure.
 */
int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        fail("expected restrict_memory range argument and optional test mode");
    }

    const MemoryRange range = parse_range(argv[1]);
    const size_t pageSize   = static_cast< size_t >(getpagesize());
    const TestMode testMode = parse_mode((3 == argc) ? argv[2] : nullptr);
    if ((range.end - range.start) < 4 * pageSize)
    {
        fail("test range is too small");
    }

    const uintptr_t firstPage  = range.start;
    const uintptr_t secondPage = range.start + pageSize;
    const uintptr_t thirdPage  = range.start + (2 * pageSize);
    const uintptr_t fourthPage = range.start + (3 * pageSize);

    if (TestMode::MremapSourceAssert == testMode)
    {
        void* firstMapping = map_hint(firstPage, pageSize);
        if (firstMapping != reinterpret_cast< void* >(firstPage))
        {
            fail("source assert mode did not receive restricted-free address");
        }
        trigger_mremap_restricted_source_assert(firstMapping, pageSize);
    }

    if (TestMode::MremapTargetAssert == testMode)
    {
        trigger_mremap_restricted_target_assert(fourthPage, pageSize);
    }

    expect_mprotect_fails_free(firstPage, pageSize);

    void* firstMapping = map_hint(firstPage, pageSize);
    if (firstMapping != reinterpret_cast< void* >(firstPage))
    {
        fail("first non-fixed hint did not return restricted-free address");
    }

    void* secondMapping = map_hint(firstPage, pageSize);
    if (secondMapping == reinterpret_cast< void* >(firstPage))
    {
        fail("second non-fixed hint reused in-use restricted address");
    }
    unmap_exact(secondMapping, pageSize);

    errno = 0;
    if (MAP_FAILED != map_noreplace(firstPage, pageSize) || EEXIST != errno)
    {
        fail("MAP_FIXED_NOREPLACE did not fail on in-use restricted address");
    }

    expect_mprotect_succeeds(firstMapping, pageSize);
    secondMapping = map_hint(firstPage, pageSize);
    if (secondMapping == reinterpret_cast< void* >(firstPage))
    {
        fail("non-fixed hint reused restricted address after mprotect");
    }
    unmap_exact(secondMapping, pageSize);
    unmap_exact(firstMapping, pageSize);
    expect_mprotect_fails_free(firstPage, pageSize);

    firstMapping = map_hint(firstPage, pageSize);
    if (firstMapping != reinterpret_cast< void* >(firstPage))
    {
        fail("hint did not reuse restricted-free address after munmap restore");
    }
    unmap_exact(firstMapping, pageSize);

    void* middleMapping = map_hint(secondPage, pageSize);
    if (middleMapping != reinterpret_cast< void* >(secondPage))
    {
        fail("partial restricted-range hint did not return middle page");
    }
    expect_mprotect_fails_free(firstPage, pageSize);
    expect_mprotect_succeeds(middleMapping, pageSize);
    expect_mprotect_fails_free(thirdPage, pageSize);
    unmap_exact(middleMapping, pageSize);

    void* noreplaceMapping = map_noreplace(thirdPage, pageSize);
    if (noreplaceMapping != reinterpret_cast< void* >(thirdPage))
    {
        fail("MAP_FIXED_NOREPLACE did not map restricted-free address");
    }
    errno = 0;
    if (MAP_FAILED != map_noreplace(thirdPage, pageSize) || EEXIST != errno)
    {
        fail("MAP_FIXED_NOREPLACE did not fail after restricted address became app-owned");
    }
    unmap_exact(noreplaceMapping, pageSize);

    std::puts("restrict_memory_semantics: success");
    return 0;
}
