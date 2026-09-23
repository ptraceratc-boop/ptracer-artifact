#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from enum import Enum


class Reg(Enum):
    """Common x86/x86_64 registers
    """

    def __new__(cls):
        obj = object.__new__(cls)
        obj._value_ = len(cls.__members__)
        return obj

    def __repr__(self):
        return "<%s.%s>" % (self.__class__.__name__, self._name_)

    def __str__(self):
        return "REG_" + self._name_

    GDI = ()
    """Refers to EDI on 32 bit and RDI on 64 bit programs
    """
    GSI = ()
    """Refers to ESI on 32 bit and RSI on 64 bit programs
    """
    GBP = ()
    """Refers to EBP on 32 bit and RBP on 64 bit programs
    """
    INST_PTR = ()
    """Refers to EIP on 32 bit and RIP on 64 bit programs
    """
    STACK_PTR = ()
    """Refers to ESP on 32 bit and RSP on 64 bit programs
    """
    GFLAGS = ()
    """Refers to EFLAGS on 32 bit and RFLAGS on 64 bit programs
    """
    GAX = ()
    """Refers to EAX on 32 bit and RAX on 64 bit programs
    """
    GBX = ()
    """Refers to EBX on 32 bit and RBX on 64 bit programs
    """
    GCX = ()
    """Refers to ECX on 32 bit and RCX on 64 bit programs
    """
    GDX = ()
    """Refers to EDX on 32 bit and RDX on 64 bit programs
    """
    CS = ()
    """CS segment register
    """
    DS = ()
    """DS segment register
    """
    ES = ()
    """ES segment register
    """
    FS = ()
    """FS segment register
    """
    GS = ()
    """GS segment register
    """
    SS = ()
    """SS segment register
    """
    EDI = ()
    """Full register on 32 bit, lower half of RDI/GDI in 64 bit programs
    """
    ESI = ()
    """Full register on 32 bit, lower half of RSI/GSI in 64 bit programs
    """
    EBP = ()
    """Full register on 32 bit, lower half of RBP/GBP in 64 bit programs
    """
    EIP = ()
    """Full register on 32 bit, lower half of RIP/INST_PTR in 64 bit programs
    """
    ESP = ()
    """Full register on 32 bit, lower half of RSP/STACK_PTR in 64 bit programs
    """
    EAX = ()
    """Full register on 32 bit, lower half of RAX/GAX in 64 bit programs
    """
    EBX = ()
    """Full register on 32 bit, lower half of RBX/GBX in 64 bit programs
    """
    ECX = ()
    """Full register on 32 bit, lower half of RCX/GCX in 64 bit programs
    """
    EDX = ()
    """Full register on 32 bit, lower half of RDX/GDX in 64 bit programs
    """

    AL = ()
    """Bits 0..7 of GAX
    """
    AH = ()
    """Bits 8..15 of GAX
    """
    AX = ()
    """Bits 0..15 of GAX
    """

    BL = ()
    """Bits 0..7 of GBX
    """
    BH = ()
    """Bits 8..15 of GBX
    """
    BX = ()
    """Bits 0..15 of GBX
    """

    CL = ()
    """Bits 0..7 of GCX
    """
    CH = ()
    """Bits 8..15 of GCX
    """
    CX = ()
    """Bits 0..15 of GCX
    """

    DL = ()
    """Bits 0..7 of GDX
    """
    DH = ()
    """Bits 8..15 of GDX
    """
    DX = ()
    """Bits 0..15 of GDX
    """

    BP = ()
    """Bits 0..15 of GBP
    """
    DI = ()
    """Bits 0..15 of GDI
    """
    SI = ()
    """Bits 0..15 of GSI
    """

    SP = ()
    """Bits 0..15 of STACK_PTR
    """
    FLAGS = ()
    """Bits 0..15 of GFLAGS
    """
    IP = ()
    """Bits 0..15 of INST_PTR
    """

    ST0 = ()
    ST1 = ()
    ST2 = ()
    ST3 = ()
    ST4 = ()
    ST5 = ()
    ST6 = ()
    ST7 = ()

    FPCW = ()
    FPSW = ()
    FPTAG = ()
    FPIP_OFF = ()
    FPIP_SEL = ()
    FPOPCODE = ()
    FPDP_OFF = ()
    FPDP_SEL = ()

    MXCSR = ()
    MXCSRMASK = ()

    MM0 = ()
    MM1 = ()    
    MM2 = ()
    MM3 = ()
    MM4 = ()
    MM5 = ()
    MM6 = ()
    MM7 = ()

    XMM0 = ()
    XMM1 = ()    
    XMM2 = ()
    XMM3 = ()
    XMM4 = ()
    XMM5 = ()
    XMM6 = ()
    XMM7 = ()

    YMM0 = ()
    YMM1 = ()    
    YMM2 = ()
    YMM3 = ()
    YMM4 = ()
    YMM5 = ()
    YMM6 = ()
    YMM7 = ()

    ZMM0 = ()
    ZMM1 = ()    
    ZMM2 = ()
    ZMM3 = ()
    ZMM4 = ()
    ZMM5 = ()
    ZMM6 = ()
    ZMM7 = ()

    K0 = ()
    K1 = ()
    K2 = ()
    K3 = ()
    K4 = ()
    K5 = ()
    K6 = ()
    K7 = ()

    DR0 = ()
    DR1 = ()
    DR2 = ()
    DR3 = ()
    DR4 = ()
    DR5 = ()
    DR6 = ()
    DR7 = ()
    DR8 = ()

    CR0 = ()
    CR1 = ()
    CR2 = ()
    CR3 = ()
    CR4 = ()

    TSSR = ()
    LDTR = ()

    TR = ()
    TR3 = ()
    TR4 = ()
    TR5 = ()
    TR6 = ()
    TR7 = ()

class Reg64(Reg):
    """64 bit only registers
    """
    RDI = ()
    RSI = ()
    RBP = ()
    RIP = ()
    RSP = ()
    RAX = ()
    RBX = ()
    RCX = ()
    RDX = ()

    R8 = ()
    R9 = ()
    R10 = ()
    R11 = ()
    R12 = ()
    R13 = ()
    R14 = ()
    R15 = ()

    DIL = ()
    """Bits 0..7 of RDI
    """
    SIL = ()
    """Bits 0..7 of RSI
    """
    BPL = ()
    """Bits 0..7 of RBP
    """
    SPL = ()
    """Bits 0..7 of RSP
    """

    R8B = ()
    """Bits 0..7 of R8
    """
    R8W = ()
    """Bits 0..15 of R8
    """
    R8D = ()
    """Bits 0..31 of R8
    """
    R9B = ()
    """Bits 0..7 of R9
    """
    R9W = ()
    """Bits 0..15 of R9
    """
    R9D = ()
    """Bits 0..31 of R9
    """
    R10B = ()
    """Bits 0..7 of R10
    """
    R10W = ()
    """Bits 0..15 of R10
    """
    R10D = ()
    """Bits 0..31 of R10
    """
    R11B = ()
    """Bits 0..7 of R11
    """
    R11W = ()
    """Bits 0..15 of R11
    """
    R11D = ()
    """Bits 0..31 of R11
    """
    R12B = ()
    """Bits 0..7 of R12
    """
    R12W = ()
    """Bits 0..15 of R12
    """
    R12D = ()
    """Bits 0..31 of R12
    """
    R13B = ()
    """Bits 0..7 of R13
    """
    R13W = ()
    """Bits 0..15 of R13
    """
    R13D = ()
    """Bits 0..31 of R13
    """
    R14B = ()
    """Bits 0..7 of R14
    """
    R14W = ()
    """Bits 0..15 of R14
    """
    R14D = ()
    """Bits 0..31 of R14
    """
    R15B = ()
    """Bits 0..7 of R15
    """
    R15W = ()
    """Bits 0..15 of R15
    """
    R15D = ()
    """Bits 0..31 of R15
    """

    XMM8 = ()
    XMM9 = ()
    XMM10 = ()
    XMM11 = ()
    XMM12 = ()
    XMM13 = ()
    XMM14 = ()
    XMM15 = ()
    XMM16 = ()
    XMM17 = ()
    XMM18 = ()
    XMM19 = ()
    XMM20 = ()
    XMM21 = ()
    XMM22 = ()
    XMM23 = ()
    XMM24 = ()
    XMM25 = ()
    XMM26 = ()
    XMM27 = ()
    XMM28 = ()
    XMM29 = ()
    XMM30 = ()
    XMM31 = ()

    YMM8 = ()
    YMM9 = ()
    YMM10 = ()
    YMM11 = ()
    YMM12 = ()
    YMM13 = ()
    YMM14 = ()
    YMM15 = ()
    YMM16 = ()
    YMM17 = ()
    YMM18 = ()
    YMM19 = ()
    YMM20 = ()
    YMM21 = ()
    YMM22 = ()
    YMM23 = ()
    YMM24 = ()
    YMM25 = ()
    YMM26 = ()
    YMM27 = ()
    YMM28 = ()
    YMM29 = ()
    YMM30 = ()
    YMM31 = ()

    ZMM8 = ()
    ZMM9 = ()
    ZMM10 = ()
    ZMM11 = ()
    ZMM12 = ()
    ZMM13 = ()
    ZMM14 = ()
    ZMM15 = ()
    ZMM16 = ()
    ZMM17 = ()
    ZMM18 = ()
    ZMM19 = ()
    ZMM20 = ()
    ZMM21 = ()
    ZMM22 = ()
    ZMM23 = ()
    ZMM24 = ()
    ZMM25 = ()
    ZMM26 = ()
    ZMM27 = ()
    ZMM28 = ()
    ZMM29 = ()
    ZMM30 = ()
    ZMM31 = ()

    TMM0 = ()
    TMM1 = ()
    TMM2 = ()
    TMM3 = ()
    TMM4 = ()
    TMM5 = ()
    TMM6 = ()
    TMM7 = ()

    CR8 = ()
