#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

info = open("script_regval.out", "w")
printRegsNow = False
stRegSize = pin.REG_Size(pin.REG_ST_BASE)

def uint_val_to_hex_str(buffer):
    hex_str = ""
    curSize = buffer.size
    while(curSize):
        curSize -= 1
        hex_str += str(hex(buffer.get_uint8(curSize)).replace("0x", ""))
      
    return "0x" + hex_str

def StartRoutine():
    global printRegsNow
    printRegsNow = True

def StopRoutine():
    global printRegsNow
    printRegsNow = False

def PrintRegisters(ctxt):
    global printRegsNow, stRegSize
    
    if printRegsNow == False:
        return
    
    reg = pin.REG_GR_BASE
    while(reg <= pin.REG_GR_LAST):
        val = ADDRINT()
        pin.PIN_GetContextRegval(ctxt, reg, val.addr())
        # Test that on reg size up to ADDRINT we get same value in PIN_GetContextRegval
        # like we get from PIN_GetContextReg return value (ADDRINT)
        assert(pin.PIN_GetContextReg(ctxt, reg) == val.value)

        info.write(f"{pin.REG_StringShort(reg)}: {hex(val.value)}\n")
        reg += 1

    reg = pin.REG_ST_BASE
    while(reg <= pin.REG_ST_LAST):
        # For the x87 FPU stack registers, using PINTOOL_REGISTER ensures a large enough buffer.
        val = BUFFER(stRegSize)
        pin.PIN_GetContextRegval(ctxt, reg, val.value)
        info.write(f"{pin.REG_StringShort(reg)}: {uint_val_to_hex_str(val)}\n")
        
        val.delete()
        
        reg += 1

def image_load(img):
    if pin.IMG_IsMainExecutable(img):
        StartRtn = pin.RTN_FindByName(img, "Start")
        assert(pin.RTN_Valid(StartRtn))
        pin.RTN_Open(StartRtn)
        pin.RTN_InsertCall(StartRtn, pin.IPOINT_BEFORE, StartRoutine)
        pin.RTN_Close(StartRtn)

        StopRtn = pin.RTN_FindByName(img, "Stop")
        assert(pin.RTN_Valid(StopRtn))
        pin.RTN_Open(StopRtn)
        pin.RTN_InsertCall(StopRtn, pin.IPOINT_AFTER, StopRoutine)
        pin.RTN_Close(StopRtn)
        
def trace_cb(trace):
    pin.TRACE_InsertCall(trace, pin.IPOINT_BEFORE, PrintRegisters, pin.IARG_CONST_CONTEXT)
    
def fini(code):
    info.close()

pin.IMG_AddInstrumentFunction(image_load)
pin.TRACE_AddInstrumentFunction(trace_cb)
pin.PIN_AddFiniFunction(fini)
