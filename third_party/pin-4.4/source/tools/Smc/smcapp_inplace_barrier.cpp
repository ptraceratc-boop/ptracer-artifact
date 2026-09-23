/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!
 * @file smcapp_inplace_barrier.cpp
 *
 * SMC test: code that modifies bytes at LABEL, followed by a specific
 * instruction, followed immediately by LABEL itself — all in one buffer.
 *
 * Usage: smcapp_inplace_barrier [barrier_type] [do_smc] [loop_count]
 *
 *   barrier_type   0 = CPUID         (Pin trace terminator)
 *                  1 = SERIALIZE     (full serializing, NOT yet a Pin trace terminator)
 *                  Default: 0 (CPUID)
 *
 *   do_smc         1 (default): patch the post-barrier bytes before each call.
 *                  0          : no patching; baseline timing.
 *
 *   loop_count     N (default 1): iterations.
 *
 * Buffer layout (x86-64):
 * ┌─────────────────────────────────────────────────────────┐
 * │ offset 0:   INSTRUCTION (CPUID/SERIALIZE)               │  ← trace should end here in smc
 * │             (CPUID wrapped in PUSH RBX / POP RBX        │
 * │              to preserve the callee-saved register)     │
 * │ offset N:   LABEL ──────────────────────────────────────│  
 * │             MOV EAX, imm32  (B8 <val> 00 00 00)         │  ← these bytes are patched
 * │             RET             (C3)                        │
 * └─────────────────────────────────────────────────────────┘
 *
 * The buffer is entered at offset 0 and called as  int (*fn)(void).
 * Before each call the bytes at LABEL are overwritten with a new return value.
 *
 * Exit code: 0 = all PASS, 1 = any FAIL.
 */

#include "smc_barriers.h"
#include "smc_util.h"
#include "../Utils/sys_memory.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>

/* 
 * PUSH RBX (53) + CPUID (0F A2) + POP RBX (5B): preserves RBX which is callee-saved
 * in both System V and Windows x64 ABIs.  CPUID clobbers EBX; 
 */
static const uint8_t kCpuidBytes[]     = {0x53, 0x0F, 0xA2, 0x5B};
static const uint8_t kSerializeBytes[] = {0x0F, 0x01, 0xE8};

struct BarrierDef
{
    const char* name;
    const uint8_t* bytes;
    int len;
    int payload_offset; /* offset of the patched payload within the buffer == len */
};

static const BarrierDef kBarriers[] = {
    {"CPUID", kCpuidBytes, (int)sizeof(kCpuidBytes), (int)sizeof(kCpuidBytes)},
    {"SERIALIZE", kSerializeBytes, (int)sizeof(kSerializeBytes), (int)sizeof(kSerializeBytes)},
};
static const int kNumBarriers = (int)(sizeof(kBarriers) / sizeof(BarrierDef));

/* ------------------------------------------------------------------ */
/* Patched payload encoding (x86-64)                                   */
/* ------------------------------------------------------------------ */
/*
 * MOV EAX, imm32 = B8 <lo> 00 00 00
 * RET            = C3
 * Total: 6 bytes.
 */
static const int kPayloadSize = 6;

static void EncodePayload(uint8_t* dst, int value)
{
    dst[0] = 0xB8; /* MOV EAX, imm32 */
    dst[1] = (uint8_t)(value & 0xFF);
    dst[2] = 0x00;
    dst[3] = 0x00;
    dst[4] = 0x00;
    dst[5] = 0xC3; /* RET */
}

/*
 * Build the executable buffer:
 *   [barrier bytes] [MOV EAX,value; RET]
 *
 * Returns the offset of the payload (== barrier length), stored in
 * *patch_offset so the caller can patch it for future calls.
 */
static int BuildBuffer(uint8_t* buf, const BarrierDef* bd, int initial_value)
{
    memcpy(buf, bd->bytes, bd->len);
    EncodePayload(buf + bd->payload_offset, initial_value);
    return bd->payload_offset + kPayloadSize;
}

/* ------------------------------------------------------------------ */
/* Call the buffer as  int (*fn)(void)                                 */
/* ------------------------------------------------------------------ */
typedef int (*IntFn)(void);

static int CallBuf(void* buf) { return ((IntFn)buf)(); }

static void RunTest(const BarrierDef* bd, int do_smc, int loop_count, int bufSize, SmcTestState& state)
{
    std::cerr << "- barrier: " << bd->name << ", do_smc=" << do_smc << '\n';
    uint8_t* buf = (uint8_t*)MemAlloc(bufSize, MEM_READ_WRITE_EXEC);
    if (!buf)
    {
        std::cerr << "FATAL: MemAlloc failed\n";
        exit(1);
    }

    if (!do_smc)
    {
        /* Write once, protect read+exec, call N times */
        const int initial_value = 42;
        BuildBuffer(buf, bd, initial_value);
        if (!MemProtect(buf, bufSize, MEM_READ_EXEC))
        {
            std::cerr << "FATAL: MemProtect(RX) failed\n";
            exit(1);
        }

        for (int i = 0; i < loop_count; ++i)
            state.CheckAndPrint(bd->name, do_smc, i, loop_count, initial_value, CallBuf(buf));

        MemProtect(buf, bufSize, MEM_READ_WRITE_EXEC); /* restore for MemFree */
        MemFree(buf, bufSize);
        return;
    }

    /* SMC mode:
     *
     * Each iteration:
     *  1. PATCH the post-barrier bytes with value A (while the region is WX).
     *  2. CALL the buffer from its start.  The buffer begins with the barrier
     *     instruction (CPUID/SERIALIZE at offset 0), which executes inside the
     *     buffer before the patched payload.
     *     · If Pin split the trace at the barrier → new trace starts at the
     *       payload and picks up the patched bytes → correct.
     *     · If Pin did NOT split → SVT check at trace entry must detect the
     *       mismatch and re-translate → correct.
     *     · If both mechanisms absent → stale translation → WRONG → FAIL.
     *  3. Repeat with value B to alternate and exercise both patch directions.
     */
    BuildBuffer(buf, bd, 1);

    for (int i = 0; i < loop_count; ++i)
    {
        EncodePayload(buf + bd->payload_offset, 1);
        state.CheckAndPrint(bd->name, do_smc, i, loop_count, 1, CallBuf(buf));

        EncodePayload(buf + bd->payload_offset, 2);
        state.CheckAndPrint(bd->name, do_smc, i, loop_count, 2, CallBuf(buf));
    }

    MemFree(buf, bufSize);
}

int main(int argc, char* argv[])
{
    const int barrier_arg = (argc > 1) ? atoi(argv[1]) : 0; /* default CPUID */
    const int do_smc      = (argc > 2) ? atoi(argv[2]) : 1;
    const int loop_count  = (argc > 3 && atoi(argv[3]) > 0) ? atoi(argv[3]) : 1;

    /* Special value -1: run all barrier types */
    const int run_all = (barrier_arg < 0 || barrier_arg >= kNumBarriers);

    int first = run_all ? 0 : barrier_arg;
    int last  = run_all ? kNumBarriers - 1 : barrier_arg;

    /* Compute buffer size from the actual barrier table: max barrier length + payload,
     * rounded up to 16 bytes for alignment. */
    int maxBarrierLen = 0;
    for (int b = 0; b < kNumBarriers; ++b)
        if (kBarriers[b].len > maxBarrierLen)
        {
            maxBarrierLen = kBarriers[b].len;
        }
    const int bufSize = (maxBarrierLen + kPayloadSize + 15) & ~15;

    std::cerr << "SMC barrier test: IN-PLACE BARRIER " << kBarriers[first].name << '-' << kBarriers[last].name
              << " do_smc=" << do_smc << "  loops=" << loop_count << '\n';

    SmcTestState state;
    for (int b = first; b <= last; ++b)
    {
        RunTest(&kBarriers[b], do_smc, loop_count, bufSize, state);
    }

    const int num_barriers_run = last - first + 1;
    const int total            = num_barriers_run * loop_count * (do_smc ? 2 : 1);
    state.PrintResult(total);
    return (state.failures > 0) ? 1 : 0;
}
