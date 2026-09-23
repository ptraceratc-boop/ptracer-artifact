#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

output_file = open("malloctrace_with_timer.out", "w")
	
num_of_allocations = 0
num_of_deallocation = 0

timer1_ID = 0
timer2_ID = 0
timer1_count = 0
timer1_running = False

def timer2_cb():
    global timer1_ID, timer1_count, timer1_running
    output_file.write(f"In timer2: timer1_count:{timer1_count}\n")
    print("In timer2_cb")
    if timer1_running:
        assert((timer1_count > 5) and (timer1_count < 20))
        output_file.write("In timer2: stopping timer1\n")
        res = pin.GLUE_TimerStop(timer1_ID)
        assert(True == res)
        timer1_running = False
    else:
        assert((timer1_count > 5) and (timer1_count < 20))

def timer1_cb():
    global num_of_allocations, num_of_deallocation, timer1_count
    output_file.write(f"timer statistics:\n num_of_allocations: {num_of_allocations}\n num_of_deallocation: {num_of_deallocation}\n")
    timer1_count += 1
 
def Arg1Before(name, size):
    global num_of_allocations, num_of_deallocation
    print("In timer1_cb")
    size = hex(size) if size else 0
    output_file.write(f"{name}({size})\n")
    if name == "malloc":
        num_of_allocations +=1
    if name == "free":
        num_of_deallocation +=1

def MallocAfter(ret, ip):
    output_file.write(f"  returns {hex(ret)}\n")

def image_instrumentation_cb(img):
    # Instrument the malloc() and free() functions.  Print the input argument
    # of each malloc() or free(), and the return value of malloc().
    # Find the malloc() function.
    mallocRtn = pin.RTN_FindByName(img, "malloc")
    if(pin.RTN_Valid(mallocRtn)):
        pin.RTN_Open(mallocRtn)
        # Instrument malloc() to print the input argument value and the return value.
        pin.RTN_InsertCall(mallocRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "malloc", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        pin.RTN_InsertCall(mallocRtn, pin.IPOINT_AFTER, MallocAfter, pin.IARG_FUNCRET_EXITPOINT_VALUE, pin.IARG_INST_PTR)

        pin.RTN_Close(mallocRtn)
	
    # Find the free() function.
    freeRtn = pin.RTN_FindByName(img, "free")
    if (pin.RTN_Valid(freeRtn)):
        pin.RTN_Open(freeRtn)
        # Instrument free() to print the input argument value.
        pin.RTN_InsertCall(freeRtn, pin.IPOINT_BEFORE, Arg1Before, pin.IARG_PYOBJ, "free", pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        pin.RTN_Close(freeRtn)

def fini(code):
    global timer1_running, timer1_ID, timer2_ID
    pin.GLUE_TimerWait(timer1_ID, -1) # Wait indefinitely (-1)
    assert(False == timer1_running)
    pin.GLUE_TimerClose(timer1_ID)
    pin.GLUE_TimerClose(timer2_ID)
    output_file.close()

pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
timer1_running = True
timer1_ID = pin.GLUE_TimerCreate(timer1_cb, 50) # 50ms interval
timer2_ID = pin.GLUE_TimerCreate(timer2_cb, 500) # 500ms interval
assert(-1 != timer1_ID and -1 != timer2_ID)
assert(timer1_ID != timer2_ID)
assert(pin.GLUE_TimerStart(timer1_ID))
assert(pin.GLUE_TimerStart(timer2_ID))
pin.PIN_AddFiniFunction(fini)
