/*
 * Copyright (C) 2016-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 *  This application should be used with the l_vdso_image tool.
 *  See documentation in the tool for the test details.
 */

#include <iostream>
#include <sys/time.h>
#include <unistd.h>



struct timeval startTime, endTime;

int main(int argc, char* argv[])
{
    gettimeofday(&startTime, NULL);
    sleep(1);
    gettimeofday(&endTime, NULL);

    long time_spent = ((endTime.tv_sec * 1000000 + endTime.tv_usec) - (startTime.tv_sec * 1000000 + startTime.tv_usec));
    std::cout << time_spent << std::endl;

    // Done.
    std::cout << "APP: Application completed successfully." << std::endl;
    return 0;
}
