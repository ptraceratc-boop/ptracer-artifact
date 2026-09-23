#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

info = open("script_pinatrace.out", "w")


def record_mem_read(ip, addr):
    info.write(f"{hex(ip)}: R {hex(addr)}\n")
    
def record_mem_write(ip, addr):
    info.write(f"{hex(ip)}: W {hex(addr)}\n")

def instruction(ins):
    memOperands = pin.INS_MemoryOperandCount(ins)
    for memOp in range(memOperands):
        if pin.INS_MemoryOperandIsRead(ins, memOp):
            pin.INS_InsertPredicatedCall(ins, pin.IPOINT_BEFORE, record_mem_read, pin.IARG_INST_PTR, pin.IARG_MEMORYOP_EA, memOp)
        if pin.INS_MemoryOperandIsWritten(ins, memOp):
            pin.INS_InsertPredicatedCall(ins, pin.IPOINT_BEFORE, record_mem_write, pin.IARG_INST_PTR, pin.IARG_MEMORYOP_EA, memOp)

def fini(code):
    info.write("#eof\n")
    info.close()

pin.INS_AddInstrumentFunction(instruction)
pin.PIN_AddFiniFunction(fini)
