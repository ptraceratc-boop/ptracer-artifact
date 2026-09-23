#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

num_threads = 0
tls_key = pin.INVALID_TLS_KEY

class ThreadData:
    def __init__(self):
        self._count = 0

def docount(c, threadid):
    tdata = pin.PIN_GetThreadData(tls_key, threadid)
    tdata._count += c
    
def thread_fini(threadid, ctxt, code):
    tdata = pin.PIN_GetThreadData(tls_key, threadid)
    print(f"Count[{threadid}] = {tdata._count}")
    del tdata
    
def thread_start(threadid, ctxt, flags):
    global num_threads
    num_threads += 1
    tdata = ThreadData()
    if not pin.PIN_SetThreadData(tls_key, tdata, threadid):
        print("PIN_SetThreadData failed")
        pin.PIN_ExitProcess(1)

def trace_cb(trace):
    bbl = pin.TRACE_BblHead(trace)
    while(pin.BBL_Valid(bbl)):
        pin.BBL_InsertCall(bbl, pin.IPOINT_ANYWHERE, docount, pin.IARG_UINT32, pin.BBL_NumIns(bbl), pin.IARG_THREAD_ID)
        
        bbl = pin.BBL_Next(bbl)

def fini(code):
    print(f"Total number of threads = :{num_threads}")

tls_key = pin.PIN_CreateThreadDataKey()
if pin.INVALID_TLS_KEY == tls_key:
    print("number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit")
    pin.PIN_ExitProcess(1)

pin.PIN_AddThreadStartFunction(thread_start)
pin.PIN_AddThreadFiniFunction(thread_fini)
pin.PIN_AddFiniFunction(fini)
pin.TRACE_AddInstrumentFunction(trace_cb)
