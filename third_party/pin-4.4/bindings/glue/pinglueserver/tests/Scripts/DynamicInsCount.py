#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

insNativeDiscoveredCount  = 0
insDynamicDiscoveredCount = 0
insNoRtnDiscoveredCount   = 0

insNativeExecutedCount    = 0 
insDynamicExecutedCount   = 0 
insNoRtnExecutedCount     = 0 

def InsNativeCount():
    global insNativeExecutedCount
    insNativeExecutedCount +=1

def InsDynamicCount():
    global insDynamicExecutedCount
    insDynamicExecutedCount +=1

def InsNoRtnCount():
    global insNoRtnExecutedCount
    insNoRtnExecutedCount +=1

def instruction(ins):
    global insNoRtnDiscoveredCount, insDynamicDiscoveredCount, insNativeDiscoveredCount
    rtn = pin.INS_Rtn(ins)
    if not pin.RTN_Valid(rtn):
        insNoRtnDiscoveredCount += 1
        pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, InsNoRtnCount)
    elif pin.RTN_IsDynamic(rtn):
        insDynamicDiscoveredCount += 1
        pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, InsDynamicCount)
    else:
        insNativeDiscoveredCount += 1
        pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, InsNativeCount)
    
def fini(code):
    global insNativeDiscoveredCount, insDynamicDiscoveredCount, insNoRtnDiscoveredCount, \
           insNativeExecutedCount, insDynamicExecutedCount, insNoRtnExecutedCount
    print(f"===============================================")
    print(f"Number of executed native instructions: {insNativeExecutedCount}")
    print(f"Number of executed dynamic instructions: {insDynamicExecutedCount}")
    print(f"Number of executed instructions without any known routine: {insNoRtnExecutedCount}")
    print(f"===============================================")
    print(f"Number of discovered native instructions: {insNativeDiscoveredCount}")
    print(f"Number of discovered dynamic instructions: {insDynamicDiscoveredCount}")
    print(f"Number of discovered instructions without any known routine: {insNoRtnDiscoveredCount}")
    print(f"===============================================")

pin.INS_AddInstrumentFunction(instruction)
pin.PIN_AddFiniFunction(fini)
