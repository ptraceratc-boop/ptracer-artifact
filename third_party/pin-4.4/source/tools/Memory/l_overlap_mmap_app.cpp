/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
constexpr size_t ALLOCATION_SIZE = 4096;

enum class Operation
{
    Mmap,
    Mprotect,
    Munmap,
    Mremap,
    MremapFixed
};

constexpr Operation ALL_OPERATIONS[] = {Operation::Mmap, Operation::Mprotect, Operation::Munmap, Operation::Mremap,
                                        Operation::MremapFixed};

bool is_pin_image_path(const std::string& path)
{
    return path.find("/pinrt/lib/libpincrt") != std::string::npos || path.find("/pinrt/lib/libpinos") != std::string::npos ||
           path.find("/pinrt/bin/pin.ld") != std::string::npos || path.find("/bin/pinbin") != std::string::npos;
}

void* parse_address(const char* addressString)
{
    char* parseEnd = nullptr;
    errno          = 0;
    auto address   = static_cast< uintptr_t >(std::strtoull(addressString, &parseEnd, 16));
    if (errno != 0 || parseEnd == addressString || *parseEnd != '\0')
    {
        return nullptr;
    }
    return reinterpret_cast< void* >(address);
}

bool parse_operation(const char* operationString, Operation& operation)
{
    if (0 == std::strcmp(operationString, "mmap"))
    {
        operation = Operation::Mmap;
    }
    else if (0 == std::strcmp(operationString, "mprotect"))
    {
        operation = Operation::Mprotect;
    }
    else if (0 == std::strcmp(operationString, "munmap"))
    {
        operation = Operation::Munmap;
    }
    else if (0 == std::strcmp(operationString, "mremap"))
    {
        operation = Operation::Mremap;
    }
    else if (0 == std::strcmp(operationString, "mremap_fixed"))
    {
        operation = Operation::MremapFixed;
    }
    else
    {
        return false;
    }
    return true;
}

void print_usage(const char* programName)
{
    std::cerr << "Usage: " << programName << " [-op mmap|mprotect|munmap|mremap|mremap_fixed] " << "[-addr <address_in_hex>]"
              << std::endl;
}


void* find_pin_mapping()
{
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line))
    {
        auto pathPos = line.find('/');
        if (pathPos == std::string::npos)
        {
            continue;
        }

        auto path = line.substr(pathPos);
        if (!is_pin_image_path(path))
        {
            continue;
        }

        auto separatorPos = line.find('-');
        if (separatorPos == std::string::npos)
        {
            continue;
        }

        return parse_address(line.substr(0, separatorPos).c_str());
    }
    return nullptr;
}

void overlap_with_mmap(void* addr)
{
    int flags    = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
    void* result = mmap(addr, ALLOCATION_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
    assert(MAP_FAILED == result);
}

void overlap_with_mprotect(void* addr)
{
    int result = mprotect(addr, ALLOCATION_SIZE, PROT_READ | PROT_WRITE);
    assert(-1 == result);
}

void overlap_with_munmap(void* addr)
{
    int result = munmap(addr, ALLOCATION_SIZE);
    assert(0 == result);
}

void overlap_with_mremap(void* addr)
{
    void* result = mremap(addr, ALLOCATION_SIZE, 2 * ALLOCATION_SIZE, MREMAP_MAYMOVE);
    assert(MAP_FAILED == result);
}

void overlap_with_mremap_fixed(void* addr)
{
    void* appMapping = mmap(nullptr, ALLOCATION_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != appMapping);

    void* result = mremap(appMapping, ALLOCATION_SIZE, ALLOCATION_SIZE, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
    assert(MAP_FAILED == result);

    int unmapResult = munmap(appMapping, ALLOCATION_SIZE);
    assert(0 == unmapResult);
}

void run_operation(Operation operation, void* addr)
{
    switch (operation)
    {
        case Operation::Mmap:
            overlap_with_mmap(addr);
            break;
        case Operation::Mprotect:
            overlap_with_mprotect(addr);
            break;
        case Operation::Munmap:
            overlap_with_munmap(addr);
            break;
        case Operation::Mremap:
            overlap_with_mremap(addr);
            break;
        case Operation::MremapFixed:
            overlap_with_mremap_fixed(addr);
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
