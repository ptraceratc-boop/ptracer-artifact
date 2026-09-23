#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

output_file = open("script_malloctrace.out", "w")
	
def Arg1Before(name, size):
    size = hex(size) if size else 0
    output_file.write(f"{name}({size})\n")

def MallocAfter(ret):
	output_file.write(f"  returns {hex(ret)}\n")

def image_instrumentation_cb(img):
    # Instrument the malloc() and free() functions.  Print the input argument
    # of each malloc() or free(), and the return value of malloc().
    # Find the malloc() function.
    mallocRtn = pin.RTN_FindByName(img, "malloc")
    if(pin.RTN_Valid(mallocRtn)):
        pin.RTN_Open(mallocRtn)
        # Instrument malloc() to print the input argument value and the return value.
        pin.RTN_InsertCall(mallocRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "malloc", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        pin.RTN_InsertCall(mallocRtn, pin.IPOINT_AFTER, MallocAfter, pin.IARG_FUNCRET_EXITPOINT_VALUE)

        pin.RTN_Close(mallocRtn)
	
    # Find the free() function.
    freeRtn = pin.RTN_FindByName(img, "free")
    if (pin.RTN_Valid(freeRtn)):
        pin.RTN_Open(freeRtn)
        # Instrument free() to print the input argument value.
        pin.RTN_InsertCall(freeRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "free", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        pin.RTN_Close(freeRtn)


pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
