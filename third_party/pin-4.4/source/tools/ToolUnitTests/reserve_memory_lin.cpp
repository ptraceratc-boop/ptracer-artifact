/*
 * Copyright (C) 2018-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <vector>
#include <string.h>

/*
 * This test application checks that memory ranges specified in a file
 * are actually mapped (visible) in the process's memory on Linux.
 *
 * It reads memory ranges from a file (one range per line, format:
 * "<start_hex> <end_hex> <desc> <tid>") and checks each page in every
 * range against the process's current memory map (/proc/self/maps).
 *
 * It has two modes:
 *
 *   1. --print-existing-mapping
 *        Scans /proc/self/maps and prints the first readable mapping
 *        it finds in the format expected by the range file. Useful for
 *        generating test input automatically.
 *
 *   2. <range-file> <access_flag>
 *        Reads ranges from the given file and checks them against the
 *        live memory map. The access_flag must be 1 to enable checking
 *        (any other value skips all checks and returns success).
 *
 *        Return codes:
 *          0 - all pages in all ranges are mapped (success)
 *          1 - none of the pages in the ranges were found mapped
 *          2 - bad arguments or file/system error
 *          3 - some pages were mapped but not all (partial mapping)
 *
*/

struct Mapping
{
    uintptr_t start;
    uintptr_t end;
};

static bool load_process_maps(std::vector< Mapping >& mappings)
{
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps)
    {
        perror("cannot open /proc/self/maps");
        return false;
    }

    char line[512];

    while (fgets(line, sizeof(line), maps))
    {
        unsigned long start = 0;
        unsigned long end   = 0;

        if (sscanf(line, "%lx-%lx", &start, &end) == 2)
        {
            mappings.push_back({static_cast< uintptr_t >(start), static_cast< uintptr_t >(end)});
        }
    }

    fclose(maps);
    return true;
}

static bool is_address_mapped(const std::vector< Mapping >& mappings, uintptr_t address)
{
    for (const Mapping& mapping : mappings)
    {
        if (address >= mapping.start && address < mapping.end)
        {
            return true;
        }
    }

    return false;
}

static int print_existing_mapping()
{
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps)
    {
        perror("cannot open /proc/self/maps");
        return 2;
    }

    char line[512];
    while (fgets(line, sizeof(line), maps))
    {
        unsigned long start = 0;
        unsigned long end   = 0;
        char perms[8]       = {};

        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) == 3)
        {
            // Pick a normal readable mapping, usually the executable/libc/ld.
            if (perms[0] == 'r' && end > start)
            {
                // Format expected by -reserve_memory file:
                // <low> <high> <desc> <tid>
                printf("%lx %lx data 0\n", start, end);
                fclose(maps);
                return 0;
            }
        }
    }

    fclose(maps);
    return 1;
}

int main(int argc, char* argv[])
{
    if (argc == 2 && strcmp(argv[1], "--print-existing-mapping") == 0)
    {
        return print_existing_mapping();
    }
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <range-file> <access_flag>\n", argv[0]);
        return 2;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f)
    {
        fprintf(stderr, "cannot open file %s\n", argv[1]);
        return 2;
    }

    int access_flag = atoi(argv[2]);

    std::vector< Mapping > mappings;
    if (!load_process_maps(mappings))
    {
        fclose(f);
        return 2;
    }

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
    {
        fprintf(stderr, "failed to get page size\n");
        fclose(f);
        return 2;
    }

    uintptr_t low  = 0;
    uintptr_t high = 0;
    int tid        = 0;
    char desc[64];

    bool knobSuccess = false;
    bool allReserved = true;

    while (fscanf(f, "%lx %lx %63s %d", &low, &high, desc, &tid) == 4)
    {
        if (high < low)
        {
            continue;
        }

        if (access_flag != 1)
        {
            continue;
        }

        for (uintptr_t address = low; address < high; address += static_cast< uintptr_t >(page_size))
        {
            if (is_address_mapped(mappings, address))
            {
                knobSuccess = true;
            }
            else
            {
                allReserved = false;
            }
        }
    }

    fclose(f);

    if (!knobSuccess && access_flag == 1)
    {
        return 1;
    }

    if (!allReserved && access_flag == 1)
    {
        return 3;
    }

    return 0;
}