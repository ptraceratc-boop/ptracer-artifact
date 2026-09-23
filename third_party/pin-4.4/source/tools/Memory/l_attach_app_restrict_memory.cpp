/*
 * Copyright (C) 2022-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 *
 * This application starts Pin with the -restrict_memory knob using address ranges
 * that it finds or creates. Pin then attaches to the parent process and instruments
 * it with the requested tool.
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string>
#include <list>
#include <assert.h>
#include <pthread.h>
#include <sstream>
#include <sys/auxv.h>
#include <fstream>
#include <sys/mman.h>

void* findVvar()
{
    std::string line;
    std::ifstream maps("/proc/self/maps");
    const std::string vvar = "[vvar]";

    while (std::getline(maps, line))
    {
        size_t idx = line.find(vvar);
        if (std::string::npos != idx && line.substr(idx) == vvar)
        {
            idx = line.find("-");
            if (std::string::npos == idx)
            {
                return NULL;
            }
            return (void*)strtoul(line.substr(0, idx).c_str(), NULL, 0x10);
        }
    }
    return NULL;
}

void* findMinMappedAddr()
{
    std::string line;
    std::ifstream maps("/proc/self/maps");
    void* min_addr = NULL;

    while (std::getline(maps, line))
    {
        size_t idx = line.find("-");
        if (std::string::npos == idx)
        {
            continue;
        }
        void* curr = (void*)strtoul(line.substr(0, idx).c_str(), NULL, 0x10);
        if (min_addr == NULL || curr < min_addr)
        {
            min_addr = curr;
        }
    }
    return min_addr;
}

/* Pin doesn't kill the process if if failed to attach, exit on SIGALRM */
void ExitOnAlarm(int sig)
{
    fprintf(stderr, "Pin is not attached, exit on SIGALRM\n");
    exit(0);
}

extern "C" int PinAttached() { return 0; }

void PrintArguments(char** inArgv)
{
    fprintf(stderr, "Going to run: ");
    for (unsigned int i = 0; inArgv[i] != 0; ++i)
    {
        fprintf(stderr, "%s ", inArgv[i]);
    }
    fprintf(stderr, "\n");
}

std::string MakeRangeString(void* start, void* end)
{
    std::stringstream ss;
    ss << start << ":" << end;
    return ss.str();
}

void CreateRangeWithMappedMiddle(std::string* pinRange, std::string* leftFreeRange, std::string* rightFreeRange)
{
    const size_t pageSize = static_cast< size_t >(getpagesize());
    void* reservation     = mmap(nullptr, 3 * pageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(reservation != MAP_FAILED);

    char* reservationStart = static_cast< char* >(reservation);
    int ret                = munmap(reservationStart, pageSize);
    assert(ret == 0);
    ret = munmap(reservationStart + 2 * pageSize, pageSize);
    assert(ret == 0);

    *pinRange       = MakeRangeString(reservationStart, reservationStart + 3 * pageSize);
    *leftFreeRange  = MakeRangeString(reservationStart, reservationStart + pageSize);
    *rightFreeRange = MakeRangeString(reservationStart + 2 * pageSize, reservationStart + 3 * pageSize);
}

/*
 * Expected command line: <this exe> -pin $PIN -none|-mapped_inside/-vdso/-vvar -pinarg <pin args > -t tool <tool args>
 */

void ParseCommandLine(int argc, char* argv[], std::list< std::string >* pinArgs)
{
    std::string pinBinary;
    int page_size = getpagesize();
    bool mappedInside = false;
    std::string pinRange;
    std::string leftFreeRange;
    std::string rightFreeRange;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = std::string(argv[i]);
        if (arg == "-pin")
        {
            pinBinary = argv[++i];
        }
        else if (arg == "-mapped_inside")
        {
            mappedInside = true;
            CreateRangeWithMappedMiddle(&pinRange, &leftFreeRange, &rightFreeRange);
            pinArgs->push_back("-restrict_memory");
            pinArgs->push_back(pinRange);
        }
        else if (arg == "-vdso")
        {
            pinArgs->push_back("-restrict_memory");
            void* vdso = (void*)getauxval(AT_SYSINFO_EHDR);
            assert(vdso);
            std::stringstream ss;
            ss << vdso << ":" << (vdso + page_size);
            pinArgs->push_back(ss.str());
        }
        else if (arg == "-vvar")
        {
            pinArgs->push_back("-restrict_memory");
            void* vvar = findVvar();
            assert(vvar);
            std::stringstream ss;
            ss << vvar << ":" << (vvar + page_size);
            pinArgs->push_back(ss.str());
        }
        // Reserve memory that intentionally misses all mapped addresses.
        else if (arg == "-none")
        {
            pinArgs->push_back("-restrict_memory");
            void* min_mapped = findMinMappedAddr();
            assert(min_mapped);
            // Intentionally miss all mapped addresses
            void* none = min_mapped - (2 * page_size);
            std::stringstream ss;
            ss << none << ":" << (none + page_size);
            pinArgs->push_back(ss.str());
        }
        else if (arg == "-pinarg")
        {
            for (int parg = ++i; parg < argc; parg++)
            {
                pinArgs->push_back(std::string(argv[parg]));
                ++i;
            }
        }
    }

    if (mappedInside)
    {
        // Note: We do not really need to send the right and left free ranges,
        // giving the one range that covers them all will be enough and give the same result.
        // This is just to make sure it doesn't crash when we have collisions.
        pinArgs->push_back("-restrict_memory");
        pinArgs->push_back(leftFreeRange);
        pinArgs->push_back("-restrict_memory");
        pinArgs->push_back(rightFreeRange);
    }

    assert(!pinBinary.empty());
    pinArgs->push_front(pinBinary);
}

void StartPin(std::list< std::string >* pinArgs)
{
    pid_t appPid = getpid();
    pid_t child  = fork();
    if (child != 0) return;

    /* here is the child */
    // sleeping to give the parent time to diminish its privileges.
    sleep(2);
    printf("resumed child \n");

    // start Pin from child
    char** inArgv = new char*[pinArgs->size() + 10];

    // Pin binary in the first
    std::list< std::string >::iterator pinArgIt = pinArgs->begin();
    std::string pinBinary                  = *pinArgIt;
    pinArgIt++;

    // build pin arguments:
    unsigned int idx = 0;
    inArgv[idx++]    = (char*)pinBinary.c_str();
    inArgv[idx++]    = (char*)"-pid";
    inArgv[idx]      = (char*)malloc(10);
    sprintf(inArgv[idx++], "%d", appPid);

    for (; pinArgIt != pinArgs->end(); pinArgIt++)
    {
        inArgv[idx++] = (char*)pinArgIt->c_str();
    }
    inArgv[idx] = 0;

    PrintArguments(inArgv);

    // since we're exiting at this point we don't free the dynmically allocated memory
    execvp(inArgv[0], inArgv);
    fprintf(stderr, "ERROR: execv %s failed\n", inArgv[0]);

    exit(1);
}

int main(int argc, char* argv[])
{
    int i;

    std::list< std::string > pinArgs;

    ParseCommandLine(argc, argv, &pinArgs);

    StartPin(&pinArgs);

    /* Exit in 20 sec */
    signal(SIGALRM, ExitOnAlarm);
    alarm(20);

    printf("Before pause, waiting on PinAttached\n");

    while (!PinAttached())
    {
        // Sleep should cause a context switch for Pin to be attached
        sleep(2);
    }

    printf("After pause\n");

    return 0;
}
/*
 *  eof
 */
