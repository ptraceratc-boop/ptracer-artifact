/*
 * Copyright (C) 2014-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <sstream>
#include <cstdlib>

#if defined(TARGET_LINUX)
#include <unistd.h>
#include <sys/types.h>
#endif //TARGET_LINUX

#if defined(TARGET_WINDOWS)
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>

#if defined(_MSC_VER)
#pragma comment(lib, "Psapi.lib")
#endif
#endif //TARGET_WINDOWS

/*
 * This application prints its memory map to stdout
 */

int main()
{
#if defined(TARGET_LINUX)
    std::ostringstream os;
    os << "/bin/cat /proc/" << getpid() << "/maps";
    system(os.str().c_str());
#elif defined(TARGET_WINDOWS)
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

        auto regionBase = reinterpret_cast< uintptr_t >(memoryInfo.BaseAddress);
        auto regionEnd  = regionBase + memoryInfo.RegionSize;
        char mappedFileName[MAX_PATH * 4] {};
        if (MEM_FREE != memoryInfo.State && nullptr != memoryInfo.AllocationBase)
        {
            GetMappedFileNameA(GetCurrentProcess(), memoryInfo.BaseAddress, mappedFileName,
                               static_cast< DWORD >(sizeof(mappedFileName)));
        }

        std::printf("%llx-%llx %lx %lx %lx %s\n", static_cast< unsigned long long >(regionBase),
                    static_cast< unsigned long long >(regionEnd), memoryInfo.State, memoryInfo.Protect, memoryInfo.Type,
                    mappedFileName);

        currentAddress = (regionEnd > currentAddress) ? regionEnd : currentAddress + systemInfo.dwPageSize;
    }
#endif

    return 0;
}
