/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
  This test checks Pin + PINOS can probably track new threads created by the application even though
  glibc recycles allocated TLS area.

  PINOS as a fast lookup table (FLT) that tracks thread based on their TLS address (fs:0/gs:0).
  Glibc doesn't always allocate a new TLS but rather recycles TLS areas from closed thread. In
  probe mode Pin doesn't track thread creation/exit events so PINOS's index is not uptodate.
  This may cause deadlocks because the thread id returned for new thread may actually belong
  to an old no longer existing thread. This was reported in PINT-6386.

  This test verifies that this issue is solved.
 */

#include "pin.H"
#include <iostream>

#include <unistd.h>
#include <syscall.h>

void before_start()
{
    // We get here before the thread start routine is called.
    // Here we check that the thread id returned by gettid and syscall(SYS_gettid)
    // are the same. If they are different then PINOS as a stale index and we abort
    int cachedTid = gettid();
    int actualTid = syscall(SYS_gettid);
    ASSERTX(cachedTid == actualTid);
}

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        RTN rtn = RTN_FindByName(img, "start");

        ASSERTX(RTN_Valid(rtn));

        (void)RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(before_start), IARG_END);
    }
}

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgramProbed();

    return 0;
}