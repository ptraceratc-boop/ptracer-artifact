#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin
allCount = 0
fallThrough = 0

def docount(hasFallThrough):
    global allCount, fallThrough
    allCount += 1
    if hasFallThrough:
        fallThrough += 1

def trace(trace):
    pin.TRACE_InsertCall(trace, pin.IPOINT_BEFORE, docount, pin.IARG_BOOL, pin.TRACE_HasFallThrough(trace))
    
def fini(code):
    global allCount, fallThrough
    print(f"{fallThrough} traces out of {allCount}  have fall-through")

pin.TRACE_AddInstrumentFunction(trace)
pin.PIN_AddFiniFunction(fini)
