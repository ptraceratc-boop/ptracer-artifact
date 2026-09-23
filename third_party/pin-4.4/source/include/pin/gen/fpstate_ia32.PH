/*
 * Copyright (C) 2010-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifdef PIN_G_FPSTATE_IA32_PH
#error duplicate inclusion of fpstate_ia32
#else
#define PIN_G_FPSTATE_IA32_PH
/*! @file
  Contains declarations of types related to the fp state on ia-32
 */

/*! @ingroup CONTEXT
 * The memory layout written by FXSAVE and read by FXRSTOR.
 * It is the legacy part of the FP state
 */
typedef UTIL::FXSAVE_IA32 FXSAVE;

/*! @ingroup CONTEXT
 * Header of the XSAVE area
 */
struct XSAVE_HEADER
{
    XSAVE_HEADER() {}
    // Active/in-use mask: each bit indicates whether the corresponding xstate component has
    // non-INIT data in this buffer.  XRSTOR uses this to decide per-component whether to restore
    // from the buffer (bit set) or reset to INIT state (bit clear).
    // NOTE: This is NOT the same as FPX_SW_BYTES._xstateBv, which is the LAYOUT mask describing
    // which features the signal frame is sized to hold (derived from XCR0).  FPX_SW_BYTES is a
    // Linux software structure embedded in the FXSAVE padding (last 48 bytes of _pad4, at offset
    // 464 within FXSAVE).  See fpstate_linux.H for its definition and accessors.
    UINT64 _xstate_bv;
    // Compaction format descriptor (XCOMP_BV):
    //  Bit 63: compact-mode flag (1 = compact layout, 0 = standard fixed-offset layout).
    //  Bits 62:0: which state components have slots in the buffer.
    // Written by XSAVEC as RFBM | (1<<63).  XSAVE/XSAVEOPT leave this field 0 (standard format).
    // XRSTOR reads bit 63 to choose standard vs compact layout, then uses bits 62:0 to locate
    // each component's offset in the compact buffer.
    UINT64 _xcomp_bv;
    UINT64 _reserved[6];
};

/*! @ingroup CONTEXT
 * Size of the reserved bytes between the XSTATE and the VSTATE parts of the FP state area,
 * as defined by the xsave layout in the SDM.
 */
const size_t VSTATE_PADDING = 128;

/*! @ingroup CONTEXT
 * The memory layout written by XSAVE and read by XRSTOR,
 * and used to hold the floating point part of the context
 * within the CONTEXT structure.
 */
struct FPSTATE
{
    // fxsave_legacy is applicable on all supported CPUs
    FXSAVE fxsave_legacy; //                                                    512  bytes at offset 0

    // The following are only applicable on processors that support XSAVE
    struct XSTATE
    {
        XSAVE_HEADER _extendedHeader; //                                        64   bytes at offset 512
        UINT8 _ymmUpper[8 * 16];      // upper 128 bits of ymm0-ymm7                  128  bytes at offset 576
        UINT8 _pad[8 * 16];           // reserved - no ymm8-ymm15 on IA-32            128  bytes at offset 704
    } _xstate;

    // The following is only applicable on processors that support AVX512
    // So far there are 832 bytes of data but the next bulk of data begins at offset 1088.
    UINT8 _reserved[VSTATE_PADDING]; //                                         128  bytes at offset 832

    // The _vstate is comprised of five parts.
    //   1. BND registers BND0-BND3 (MPX) OR APX extended GPRs r16-r31 (mutually exclusive)
    //   2. BND control and status registers (MPX, part of APX EGPR area on APX-enabled CPUs)
    //   3. The Opmask registers k0-k7
    //   4. The upper 256 bits of zmm0-zmm7 (and padding for zmm8-zmm15)
    //   5. reserved - no zmm16-zmm31 on IA32
    struct VSTATE
    {
        // MPX (components 3-4) and APX EGPR (component 19) share the same XSAVE offset (960).
        // They are mutually exclusive: a processor supports one or neither, never both.
        //
        // On a 32-bit process the EGPRs r16-r31 are NOT architecturally accessible (they require
        // REX2/EVEX encodings that only exist in 64-bit mode), so this _egpr slot is never the
        // active register file here. But like XTILE_CFG below, component 19 still appears in the
        // XSAVE layout for 32-bit processes on APX-capable CPUs because XCR0 is set globally by
        // the kernel. Its in-use bit in _xstate_bv stays 0, so XSAVE/XRSTOR treat it as INIT
        // (zero) state and copy nothing meaningful - but the 128 bytes must still be reserved so
        // the offsets of the following components (_kmasks at 1088, etc.) stay correct. The
        // _egpr name is kept only to document the slot; the usable EGPR layout lives in
        // fpstate_ia32e.PH.
        union
        {
            struct
            {
                UINT8 _bndRegs[64]; // BND registers BND0-BND3.                  64   bytes at offset 960
                UINT8 _bndCSR[64];  // BND control and status registers.         64   bytes at offset 1024
            } _mpx;
            UINT64 _egpr[16]; // APX EGPR slot r16-r31 (inaccessible in 32-bit). 128  bytes at offset 960
        };
        UINT64 _kmasks[8];       // 8 64-bit k (opmask) registers                64   bytes at offset 1088
        UINT8 _zmmUpper[8 * 32]; // upper 256 bits of zmm0-zmm7                  256  bytes at offset 1152
        UINT8 _pad256[8 * 32];   // reserved - no zmm8-zmm15 on IA-32            256  bytes at offset 1408
        UINT8 _pad1024[16 * 64]; //                                              1024 bytes at offset 1664
    } _vstate;

    // The following is only applicable on processors that support AMX (XTILE_CFG, xstate bit 17).
    // XTILE_CFG is a non-dynamic feature included in the kernel's signal frame xstate area.
    // Even though AMX tile instructions are Intel64-only, XTILE_CFG appears in XCR0 for 32-bit
    // processes on AMX-capable CPUs (the kernel sets XCR0 globally). CPUID reports XTILE_CFG at
    // absolute offset 2752 with size 64 on SPR, so there is a 64-byte gap after VSTATE.
    //
    // Note: XTILE_DATA (xstate bit 18, 8KB of tile register data) is intentionally excluded.
    //
    UINT8 _xtileCfgPadding[64]; // padding                                  64   bytes at offset 2688
    UINT8 _xtileCfg[64];        // TILECFG register (AMX configuration)     64   bytes at offset 2752

    // Total of 2816 bytes.
};

/*! @ingroup CONTEXT
 *  Size of Pin's (full) FP state structure.
 *  It is larger than the size of the structure used by the fxsave instruction (see FXSAVE).
 *  It may be larger than the size of the structure used by the xsave instruction, depending on the features enabled in the
 *  xstate_bv field in the extended header.
 */
const size_t FPSTATE_SIZE = sizeof(FPSTATE);

/*! @ingroup CONTEXT
 *  This is the alignment that the structure used to save the FPSTATE
 *  The alignment required by xsave is 64, the alignment required by
 *  fxsave is 16 - so we use the larger
 */
const size_t FPSTATE_ALIGNMENT = 64;

#endif // PIN_G_FPSTATE_IA32_PH
