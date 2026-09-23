#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin
total = 0
num_of_traces = 0
#info = open("inscount.out", "w")

def instrumentation_count(trace):
    global total, num_of_traces
    head = pin.TRACE_BblHead(trace)
    addr = pin.BBL_Address(head)
    insts = pin.BBL_NumIns(head)
    size = pin.BBL_Size(head)
    total += insts
    num_of_traces +=1
    #info.write(f"Basic Block @ {addr} SIZE: {size} NUM INS= IN BLOCK: {insts}  TOTAL: {total}\n")

def fini(code):
    global total, num_of_traces
    print(f"The total number of instructions are: {total}. Number of instrumented traces are: {num_of_traces} ")

pin.TRACE_AddInstrumentFunction(instrumentation_count)
pin.PIN_AddFiniFunction(fini)
