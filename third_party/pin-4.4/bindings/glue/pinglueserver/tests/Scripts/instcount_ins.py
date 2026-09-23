#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin
total = 0

def count():
    global total
    total += 1

def instruction_instrumented_func(ins):
    pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, count)
    
def fini(code):
    global total
    print(f"The total number of executed instructions are:{total}")

pin.INS_AddInstrumentFunction(instruction_instrumented_func)
pin.PIN_AddFiniFunction(fini)
