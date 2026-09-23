/*
 * Copyright (C) 2019-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 *  This application branch into the vsyscall area (used only in 64 bits)
 *  Used for testing Pin handling when application branch into that area
 */

#include <iostream>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>



#define VSYSCALL_GETTIMEOFDAY_ADDRESS 0xffffffffff600000

struct timeval startTime, endTime;

typedef long (*vsyscall_gettimeofday_t)(struct timeval*, struct timezone*);
const vsyscall_gettimeofday_t vgtod = (vsyscall_gettimeofday_t)(VSYSCALL_GETTIMEOFDAY_ADDRESS);

//vsyscall time address is 0xffffffffff600400
//vsyscall getcpu address is 0xffffffffff600800

int main(int argc, char* argv[])
{
    int ret = 0;

    ret = vgtod(&startTime, NULL);
    std::cout << "App  vsyscall return value : " << ret << std::endl;
    assert(ret == 0);

    sleep(1);
    ret = vgtod(&endTime, NULL);
    std::cout << "App  vsyscall return value : " << ret << std::endl;
    assert(ret == 0);

    long time_spent = ((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec));
    std::cout << std::endl << "Time Spent = " << time_spent << std::endl;

    // Done.
    std::cout << "Application completed successfully." << std::endl;

    return 0;
}
