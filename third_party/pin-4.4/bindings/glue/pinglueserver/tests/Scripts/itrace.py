#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

info = open("script_itrace.out", "w")

def printip(ip):
    info.write(hex(ip) + "\n")

def instruction(ins):
    pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, printip, pin.IARG_INST_PTR)

def fini(code):
    info.write("#eof\n")
    info.close()

pin.INS_AddInstrumentFunction(instruction)
pin.PIN_AddFiniFunction(fini)
