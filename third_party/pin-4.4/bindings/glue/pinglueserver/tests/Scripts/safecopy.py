#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin, c

info = open("script_safecopy.out", "w")

def do_load(reg, addr):
    value = ADDRINT()
    pin.PIN_SafeCopy(value.addr(), c.void_p(addr), value.size)
    info.write(f"Emulate loading from addr {hex(addr)} to {pin.REG_StringShort(reg)}\n" )
    
    return value.value
    
def emulate_load(ins):
    if (pin.INS_Opcode(ins) == pin.XED_ICLASS_MOV) and pin.INS_IsMemoryRead(ins) \
                and pin.INS_OperandIsReg(ins, 0) and pin.INS_OperandIsMemory(ins, 1):
        pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, do_load, pin.IARG_UINT32, pin.INS_OperandReg(ins, 0), \
                        pin.IARG_MEMORYREAD_EA, pin.IARG_RETURN_REGS, pin.INS_OperandReg(ins, 0))

        pin.INS_Delete(ins)

def fini(code):
    info.write("TOOL: Generate pin exception\n")
    _to = 0
    _from = 0
    exptInfo = EXCEPTION_INFO()
    toCopy = 1024
    
    if pin.PIN_SafeCopyEx(c.void_p(_to), c.void_p(_from), toCopy, exptInfo.addr()) != toCopy:
        if pin.PIN_GetExceptionCode(exptInfo.addr()) != pin.EXCEPTCODE_ACCESS_INVALID_ADDRESS:
            info.write("TOOL ERROR: PIN_SafeCopyEx returned with an unexpected exception code\n")
            info.write(f"{pin.PIN_ExceptionToString(exptInfo.addr())}\n")
            return
        
        exptAddr = pin.PIN_GetExceptionAddress(exptInfo.addr())
        if exptAddr != 0:
            info.write("TOOL ERROR: Exception from PIN_SafeCopyEx returned with non NULL address\n")
            return
        else:
            info.write("TOOL: PIN_SafeCopyEx failed as expected\n")
            info.write(f"{pin.PIN_ExceptionToString(exptInfo.addr())}\n")
    
    info.close()

pin.INS_AddInstrumentFunction(emulate_load)
pin.PIN_AddFiniFunction(fini)
