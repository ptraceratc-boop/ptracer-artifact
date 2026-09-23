/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file smc_barrier_verify.cpp
 *
 * PinTool that counts CPUID and SERIALIZE instructions encountered during JIT
 * compilation, classifying each occurrence as either:
 *
 *   terminated — the instruction is the last one in its trace, i.e. Pin split
 *                the trace at that instruction (trace-terminator behaviour).
 *   embedded   — the instruction appears mid-trace; Pin did not split there.
 *
 * Purpose: verify the PINT-5754 optimization.
 *   On a non-writable (RX) page:
 *     -smc_strict on  → CPUID/SERIALIZE MUST terminate the trace  (terminated >= 1)
 *     -smc_strict off → CPUID/SERIALIZE must NOT terminate the trace (terminated == 0)
 *
 * Output written to the file named by the -out knob (one line):
 *   cpuid_terminated=N cpuid_embedded=M serialize_terminated=P serialize_embedded=Q
 *
 * Typical test invocation (barrier_type=0 = CPUID, do_smc=0, loops=1):
 *   pin -smc_strict -t smc_barrier_verify -out strict.out   -- smcapp_barrier 0 0 1
 *   pin            -t smc_barrier_verify -out nostrict.out -- smcapp_barrier 0 0 1
 *   grep 'cpuid_terminated=[1-9]' strict.out    # terminated: OK
 *   grep 'cpuid_terminated=0 '    nostrict.out  # not terminated: OK
 */

#include "pin.H"
#include <fstream>

static KNOB< std::string > KnobOutFile(KNOB_MODE_WRITEONCE, "pintool", "out", "smc_barrier_verify.out",
                                       "output file for barrier trace termination counts");

/* Counters updated during JIT (single-threaded, no atomics needed). */
static UINT64 cpuid_terminated     = 0; /*!< CPUID is the last ins of its trace */
static UINT64 cpuid_embedded       = 0; /*!< CPUID appears mid-trace            */
static UINT64 serialize_terminated = 0; /*!< SERIALIZE is the last ins of its trace */
static UINT64 serialize_embedded   = 0; /*!< SERIALIZE appears mid-trace            */

/*!
 * TRACE instrumentation callback.
 * For each instruction in the trace, check whether it is CPUID or SERIALIZE
 * and whether it is the trace tail (terminator) or embedded inside the trace.
 */
static VOID OnTrace(TRACE trace, VOID* /*unused*/)
{
    /* Find the last BBL of the trace, then its tail instruction. */
    BBL last_bbl = TRACE_BblHead(trace);
    while (BBL_Valid(BBL_Next(last_bbl)))
        last_bbl = BBL_Next(last_bbl);
    const INS trace_tail = BBL_InsTail(last_bbl);

    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins))
        {
            if (INS_Opcode(ins) == XED_ICLASS_CPUID)
            {
                if (ins == trace_tail)
                {
                    ++cpuid_terminated;
                }
                else
                {
                    ++cpuid_embedded;
                }
            }
            else if (INS_Opcode(ins) == XED_ICLASS_SERIALIZE)
            {
                if (ins == trace_tail)
                {
                    ++serialize_terminated;
                }
                else
                {
                    ++serialize_embedded;
                }
            }
        }
    }
}

static VOID Fini(INT32 /*code*/, VOID* /*unused*/)
{
    std::ofstream out(KnobOutFile.Value().c_str());
    out << "cpuid_terminated=" << cpuid_terminated << " cpuid_embedded=" << cpuid_embedded
        << " serialize_terminated=" << serialize_terminated << " serialize_embedded=" << serialize_embedded << '\n';
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);
    TRACE_AddInstrumentFunction(OnTrace, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
    return 0;
}
