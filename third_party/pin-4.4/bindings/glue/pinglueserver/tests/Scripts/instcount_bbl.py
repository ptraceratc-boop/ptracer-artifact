#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin
total = 0

def docount(c):
    global total
    total += c
    

def trace_instrumentation_cb(trace):
    bbl = pin.TRACE_BblHead(trace)
    while(pin.BBL_Valid(bbl)):
        pin.BBL_InsertCall(bbl, pin.IPOINT_BEFORE, docount, pin.IARG_UINT32, pin.BBL_NumIns(bbl))
        bbl = pin.BBL_Next(bbl)
    
def fini(code):
    global total
    print(f"The total number of instructions is: {total}")


pin.TRACE_AddInstrumentFunction(trace_instrumentation_cb)
pin.PIN_AddFiniFunction(fini)
