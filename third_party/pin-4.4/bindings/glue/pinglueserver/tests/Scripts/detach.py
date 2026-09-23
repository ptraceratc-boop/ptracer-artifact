#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

icount = 0
N = 10000

def docount():
    global icount, N
    icount+=1
    
    if 0 == (icount % N):
        pin.PIN_Detach()

def instruction(ins):
    pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, docount)

def bye_world():
    global N
    print(f"Detached at icount = {N}")

pin.INS_AddInstrumentFunction(instruction)
pin.PIN_AddDetachFunction(bye_world)
