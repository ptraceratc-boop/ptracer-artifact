/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file smcapp_barrier.cpp
 *
 * SMC test application: standalone serializing barriers (CPUID, SERIALIZE).
 *
 * Usage: smcapp_barrier [barrier_type] [do_smc] [loop_count]
 *
 *   barrier_type   0 = CPUID     (Intel SDM vol.3 §8.3 full serializing instruction;
 *                                 Pin trace terminator on writable code regions)
 *                  1 = SERIALIZE (SERIALIZE instruction, 0F 01 E8; full serializing;
 *                                 see PINT-5754 for trace-terminator status)
 *                  -1 = all      run all barriers in order (default)
 *
 *   do_smc         1 (default): SMC mode — write new code bytes before every
 *                               execution so Pin must re-translate after each
 *                               barrier trace terminator.
 *               0             : no-SMC mode — write code once, protect the
 *                               buffer as read+execute, then repeat barrier+call
 *                               N times.  Useful as a performance baseline to
 *                               measure trace-termination overhead without
 *                               actual code modification (see PINT-5754).
 *
 *   loop_count  N (default 10): number of SMC or no-SMC cycles to run.
 *
 * Correctness:
 *   Each call to the buffer captures the actual string written by the
 *   position-independent function ("foo" or "bar") and compares it to the
 *   expected value.  If Pin incorrectly re-uses a stale cached trace the
 *   returned string will not match the expected value and the test prints
 *   "FAIL" for that iteration, then exits with a non-zero code.
 *
 *   Example correct output (barrier_type=0 do_smc=1):
 *     [CPUID][SMC=1][ 1/10] write=foo expected=foo got=foo -> PASS
 *     [CPUID][SMC=1][ 1/10] write=bar expected=bar got=bar -> PASS
 *
 *   Example incorrect output (Pin bug — stale trace):
 *     [CPUID][SMC=1][ 2/10] write=bar expected=bar got=foo -> FAIL ***
 *
 * Exit code: 0 on full success, 1 if any correctness failure is detected.
 */

#include "smc_barriers.h"
#include "smc_util.h"

namespace
{

struct BarrierDef
{
    const char* name;
    void (*fn)(void);
};

static const BarrierDef kBarriers[] = {
    {"CPUID", smc_barrier_cpuid},         /* barrier_type 0 */
    {"SERIALIZE", smc_barrier_serialize}, /* barrier_type 1 */
};

static const int kNumBarriers = static_cast< int >(sizeof(kBarriers) / sizeof(kBarriers[0]));

} // namespace

int main(int argc, char* argv[])
{
    const int barrier_type = (argc > 1) ? atoi(argv[1]) : -1;
    const int do_smc       = (argc > 2) ? atoi(argv[2]) : 1;
    const int loop_count   = (argc > 3 && atoi(argv[3]) > 0) ? atoi(argv[3]) : 10;

    if (barrier_type >= 0 && barrier_type < kNumBarriers)
    {
        return RunBarrierTest(kBarriers[barrier_type].name, kBarriers[barrier_type].fn, do_smc, loop_count);
    }

    /* -1 (or any other out-of-range value): run all barriers in order. */
    int failures = 0;
    for (int i = 0; i < kNumBarriers; ++i)
    {
        failures += RunBarrierTest(kBarriers[i].name, kBarriers[i].fn, do_smc, loop_count);
    }
    return failures ? 1 : 0;
}
