#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

mallocCount = 0

info = open("script_malloc_mt.out", "w")

def thread_start(threadid, ctxt, flags):
    info.write(f"thread begin {threadid}\n")

def thread_fini(threadid, ctxt, code):
    info.write(f"thread end {threadid} code {code}\n")

def BeforeMalloc(size, threadid):
    global mallocCount
    mallocCount += 1
    info.write(f"[{mallocCount}]: thread {threadid} entered malloc({size})\n")

def image_load(img):
    rtn = pin.RTN_FindByName(img, "malloc")
    if pin.RTN_Valid(rtn):
        pin.RTN_Open(rtn)
        pin.RTN_InsertCall(rtn, pin.IPOINT_BEFORE, BeforeMalloc, pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0, pin.IARG_THREAD_ID)
        pin.RTN_Close(rtn)

def fini(code):
    info.close()

pin.IMG_AddInstrumentFunction(image_load)
pin.PIN_AddThreadStartFunction(thread_start)
pin.PIN_AddThreadFiniFunction(thread_fini)
pin.PIN_AddFiniFunction(fini)
