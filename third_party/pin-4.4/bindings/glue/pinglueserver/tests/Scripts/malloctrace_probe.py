#
# Copyright (C) 2024-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

mallocCount = 0

output_file = open("script_malloctrace_probe.out", "w")

def BeforeExit():
    output_file.close()

def Arg1Before(name, size):
    size = hex(size) if size else 0
    output_file.write(f"{name}({size})\n")

def MallocAfter(ret):
    global mallocCount
    mallocCount += 1
    output_file.write(f"  returns {hex(ret)} [{mallocCount}]\n")

def image_instrumentation_cb(img):
    mallocRtn = pin.RTN_FindByName(img, "malloc")
    if(pin.RTN_Valid(mallocRtn)):
        assert(pin.RTN_IsSafeForProbedInsertion(mallocRtn))
        pin.RTN_InsertCallProbed(mallocRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "malloc", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        
        proto_malloc_after = pin.PROTO_Allocate(pin.PIN_PARG_POINTER, pin.CALLINGSTD_DEFAULT, "malloc", pin.PIN_PARG_SIZE_T)
        pin.RTN_InsertCallProbed(mallocRtn, pin.IPOINT_AFTER, MallocAfter, pin.IARG_PROTOTYPE, proto_malloc_after, pin.IARG_FUNCRET_EXITPOINT_VALUE)
	
    freeRtn = pin.RTN_FindByName(img, "free")
    if (pin.RTN_Valid(freeRtn)):
        assert(pin.RTN_IsSafeForProbedInsertion(freeRtn))
        pin.RTN_InsertCallProbed(freeRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "free", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
    
    exitRtn = pin.RTN_FindByName(img, "_exit")
    if (pin.RTN_Valid(exitRtn)):
        pin.RTN_InsertCallProbed(exitRtn, pin.IPOINT_BEFORE, BeforeExit)

pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
