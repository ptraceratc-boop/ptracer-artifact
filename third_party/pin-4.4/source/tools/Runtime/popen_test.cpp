/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "pin.H"

KNOB< std::string > knobCmd(KNOB_MODE_WRITEONCE, "pintool", "cmd", "", "Command line to run");

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    if (knobCmd.Value() == "")
    {
        printf("Must specify command line to run using the -cmd knob!\n");
        PIN_ExitApplication(2);
    }
    char buf[4096];
    FILE* f = popen(knobCmd.Value().c_str(), "r");
    if (NULL == f)
    {
        printf("popen failed for <%s> - %d\n", knobCmd.Value().c_str(), errno);
        PIN_ExitApplication(1);
    }
    while (NULL != fgets(buf, sizeof(buf), f))
    {
        if (strlen(buf) >= 9 && buf[3] == 'x' && buf[4] == 'r')
        {
            fputs(buf, stderr);
        }
    }

    int estat = pclose(f);
    if (0 != estat)
    {
        printf("Exit status of popen() was %d\n", estat);
        PIN_ExitApplication(estat);
    }

    // Never returns
    PIN_StartProgram();

    return 0;
}
