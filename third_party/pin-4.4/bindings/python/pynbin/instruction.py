#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from __future__ import annotations

from .request import Instrumentation, InstrumentationType
from .image import Img
from .routine import Rtn
from .callbacks import GlueCallback
from .opcode import Opcode
from .registers import Reg


class Ins(Instrumentation):
    """This class is used to specify instruction instrumentation conditions
       and operations.

       All conditions for an instruction are combined using the AND operator.
       To combine conditions using other operations the combine() and negate()
       static methods can be used.
       The  negate() method will return a copy of the given Ins object where the
       resulting combined condition is negated.
       The combine() will return a copy of two or more Ins objects combined using
       operations from the 
    """

    def instrumentation_type(self) -> InstrumentationType:
        """ Override instrumentation_type.
        
        Returns:
             InstrumentationType.INS
        """
        return InstrumentationType.INS

    def delete(self) -> Ins:
        """Mark the original instruction for deletion

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def insert_after(self, callback: GlueCallback, *args) -> Ins:
        """Insert a callback after the instruction

        Args:
            callback    A GlueCallback to call before the instruction is executed
            *args       Arguments to pass to the callback. All arguments in the list must be derived
                        from Arg
        
        Raises:
            TypeError
            ValueError

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def insert_before(self, callback: GlueCallback, *args) -> Ins:
        """Insert a callback before the instruction

        Args:
            callback    A GlueCallback to call before the instruction is executed
            *args       Arguments to pass to the callback. All arguments in the list must be derived
                        from Arg
        
        Raises:
            TypeError
            ValueError

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def insert_branch_taken(self, callback: GlueCallback, *args) -> Ins:
        """Insert a callback after a branch instruction was taken

        This callback will only be called for instructions that satisfy is_valid_for_taken_branch.
        The caller of this function is not required to check for this condition explicitly.

        Args:
            callback    A GlueCallback to call before the instruction is executed
            *args       Arguments to pass to the callback. All arguments in the list must be derived
                        from Arg
        
        Raises:
            TypeError
            ValueError

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self
    
    def in_image(self, img: Img | str) -> Ins:
        """_summary_

        Args:
            img (Img): The image instrumentation object to filter by.
                       If a string is passed we construct the filter as:
                       img = Img().name(img)

        Raises:
            TypeError: img is not of type Img or not of type str
    
        Returns:
            The Ins object (self)
        """
        raise NotImplementedError()

        return self

    def in_rtn(self, rtn: Rtn | str) -> Ins:
        """_summary_

        Args:
            rtn (Rtn): The routine instrumentation object to filter by.
                       If a string is passed we construct the filter as:
                       rtn = Rtn().name(rtn)
        Raises:
            TypeError: rtn is not of type Rtn or not of type str

        Returns:
            The Ins object (self)
        """
        raise NotImplementedError()

        return self

    def has_explicit_memory_reference(self) -> Ins:
        """Filter instructions that have an explicit memory operand.

        This call limits instrumentation to instructions that have an explicit memory operand.
        
        See INS_HasExplicitMemoryReference() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_address_size_prefix(self) -> Ins:
        """Filter instructions that have an address size prefix.

        This call limits instrumentation to instructions that have an address size prefix.
        
        See INS_AddressSizePrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_branch_not_taken_prefix(self) -> Ins:
        """Filter instructions that have a branch not taken hint prefix.

        This call limits instrumentation to instructions that have a branch not taken hint prefix.
        
        See INS_BranchNotTakenPrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_branch_taken_prefix(self) -> Ins:
        """Filter instructions that have a branch taken hint prefix.

        This call limits instrumentation to instructions that have a branch taken hint prefix.
        
        See INS_BranchTakenPrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_fall_through(self) -> Ins:
        """Filter instructions that "naturally" execute the next instruction.

        This call limits instrumentation to instructions that don't modify the control flow (most instructions) and
        to instructions that may or may not change the control flow (like conditional branches).
        Note that an unconditional branch or call to the next instruction still doesn't have a fall-through, 
        since a fall through is the implicitly following instruction, and an unconditional branch never uses that.
        
        See INS_HasFallThrough() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_lock_prefix(self) -> Ins:
        """Filter instructions that have LOCK prefix.

        This call limits instrumentation to instructions that have LOCK prefix
        
        See INS_LockPrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_memory_read2(self) -> Ins:
        """Filter memory read instructions that have two read operands.

        This call limits instrumentation to memory read instructions that have two read operands.
        
        See INS_HasMemoryRead2() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_memory_vector(self) -> Ins:
        """Filter memory read instructions that have memory vector operands.

        This call limits instrumentation to memory read instructions that have  memory vector operands.
        
        See INS_HasMemoryVector() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_operand_size_prefix(self) -> Ins:
        """Filter memory read instructions that have an operand size prefix.

        This call limits instrumentation to instructions that have an operand size prefix.
        
        See INS_OperandSizePrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_real_rep(self) -> Ins:
        """Filter instructions that have a real REP/REPZ/REPNZ prefix.

        This call limits instrumentation to instructions that have a real repetition prefix
        and not just one that is part of the binary encodeing but does not cause repetition.
        
        See INS_HasRealRep() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_rep_prefix(self) -> Ins:
        """Filter instructions that have a REP (0xF3) prefix.

        This call limits instrumentation to instructions that have REP (0xF3) prefix
        
        See INS_RepPrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_repne_prefix(self) -> Ins:
        """Filter instructions that have a REPNE (0xF2) prefix.

        This call limits instrumentation to instructions that have REPNE (0xF2) prefix
        
        See INS_RepnePrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_rep_prefix(self) -> Ins:
        """Filter instructions that have a REP (0xF3) prefix.

        This call limits instrumentation to instructions that have REP (0xF3) prefix
        
        See INS_RepPrefix() in Pin API Reference.

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_scatter_memory_access(self) -> Ins:
        """Filter instructions that have scatter memory access.

        This call limits instrumentation to instructions that have scatter/gather memory access semantics
        like vscatter/vgather instruction.

        See INS_HasScatteredMemoryAccess() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def has_segment_prefix(self) -> Ins:
        """Filter instructions that have a segment prefix.

        This call limits instrumentation to instructions that have a segment prefix.

        See INS_SegmentPrefix() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_amx(self) -> Ins:
        """Filter AMX instructions

        This call limits instrumentation to AMX instructions.

        See INS_IsAmx() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_atomic_update(self) -> Ins:
        """Filter atomic instructions
        
        This call limits instrumentation to atomic instructions.
        Note that not all atomic instructions must have the LOCK prefix.
        For instance XCHG is atomic with or without the LOCK prefix.

        See INS_IsAtomicUpdate() in Pin API Reference
        
        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_branch(self) -> Ins:
        """Filter branch instruction

        This call limits instrumentation to branch instructions (conditional or unconditional)
        
        See INS_IsBranch() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_cacheline_flush(self) -> Ins:
        """Filter instructions that cause cache line flush

        This call limits instrumentation to instructions that cause cache line flush
        
        See INS_IsCacheLineFlush() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_call(self) -> Ins:
        """Filter call instructions

        This call limits instrumentation to call instruction
        
        See INS_IsCall() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_control_flow(self) -> Ins:
        """Filter control flow instructions

        This call limits instrumentation to control flow instructions.
        Control flow instructions are any branch, call, ret instructions.
        This function will also consider Xbegin and Xend as control flow instructions.
        
        See INS_IsControlFlow() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_direct_branch(self) -> Ins:
        """Filter direct branch instructions

        This call limits instrumentation to direct branch instructions.
        Direct branch instructions are branch instructions that have a near
        relative branch target (offset from IP or immediate)
        
        See INS_IsDirectBranch() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_direct_call(self) -> Ins:
        """Filter direct call instructions

        This call limits instrumentation to direct call instructions.
        Direct call instructions are call instructions that have a near
        relative branch target (offset from IP or immediate)
        
        See INS_IsDirectCall() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_direct_control_flow(self) -> Ins:
        """Filter direct control flow instructions

        This call limits instrumentation to direct control flow instructions.
        Direct control flow instructions are control flow instructions that have a near
        relative branch target (offset from IP or immediate)
        
        See INS_IsDirectControlFlow() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_direct_far_jump(self) -> Ins:
        """Filter direct far jump instructions

        This call limits instrumentation to direct far jump instructions.
        A direct far jump is a far jump with the jump target specified as an
        immediate.

        See INS_IsDirectFarJump() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_far_call(self) -> Ins:
        """Filter far call instructions

        This call limits instrumentation to far call instructions.

        See INS_IsFarCall() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_far_jump(self) -> Ins:
        """Filter far jump instructions

        This call limits instrumentation to far jump instructions.

        See INS_IsFarJump() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_far_ret(self) -> Ins:
        """Filter far ret instructions

        This call limits instrumentation to far ret instructions.

        See INS_IsFarRet() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_halt(self) -> Ins:
        """Filter HLT or UD2 instructions

        This call limits instrumentation to HLT or UD2 instructions.

        See INS_IsHalt() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_indirect_control_flow(self) -> Ins:
        """Filter indirect control flow instructions

        This call limits instrumentation to indirect control flow instructions.
        Indirect control flow instructions are control flow instructions that have the
        target address provided through memory or register.
        
        See INS_IsIndirectControlFlow() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_interrupt(self) -> Ins:
        """Filter interrupt instructions

        This call limits instrumentation to interrupt category instructions.
        
        See INS_IsInterrupt() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_ip_rel_read(self) -> Ins:
        """Filter IP relative memory reads

        This call limits instrumentation to IP relative memory read instruction.
        
        See INS_IsIpRelRead() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_ip_rel_write(self) -> Ins:
        """Filter IP relative memory writes

        This call limits instrumentation to IP relative memory write instruction.
        
        See INS_IsIpRelWrite() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_iret(self) -> Ins:
        """Filter IRET instruction

        This call limits instrumentation to IRET instruction.
        
        See INS_IsIRet() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_lea(self) -> Ins:
        """Filter LEA instruction

        This call limits instrumentation to LEA instruction.
        
        See INS_IsLea() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_mask_move(self) -> Ins:
        """Filter mask move instructions

        This call limits instrumentation to mask move instructions.
        These instructions are maskmovq and maskmovdqu.
        
        See INS_IsMaskMov() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_memory_read(self) -> Ins:
        """Filter memory read instructions

        This call limits instrumentation to memory read instructions.

        See INS_IsMemoryRead() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_memory_write(self) -> Ins:
        """Filter memory write instructions

        This call limits instrumentation to memory write instructions.

        See INS_IsMemoryWrite() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_mov(self) -> Ins:
        """Filter move instructions

        This call limits instrumentation to move instructions.

        See INS_IsMov() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_mov_full_reg_reg_same(self) -> Ins:
        """Filter mov reg,reg where reg is a full register

        This call limits instrumentation to the mov reg,reg where reg is a full register instruction.

        See INS_IsMovFullRegRegSame() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_nop(self) -> Ins:
        """Filter NOP instructions

        This call limits instrumentation to NOP instructions.

        See INS_IsNop() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_pc_materialization(self) -> Ins:
        """Filter call to next instruction

        This call limits instrumentation to a call instructions with the call target as the next instruction.

        See INS_IsPcMaterialization() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_predicated(self) -> Ins:
        """Filter predicated conditional move instructions

        This call limits instrumentation to predicated move instructions.
        These instructions include CMOVcc, FCMOVcc and rep string ops since they don't
        execute if GCX == 0.

        See INS_IsPredicated() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_prefetch(self) -> Ins:
        """Filter prefetch instructions

        This call limits instrumentation to prefetch instructions.

        See INS_IsPrefetch() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_prefetch(self) -> Ins:
        """Filter prefetch instructions

        This call limits instrumentation to prefetch instructions.

        See INS_IsPrefetch() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_rdtsc(self) -> Ins:
        """Filter RDTSC/RDTSCP instructions

        This call limits instrumentation RDTSC/RDTSCP instructions.

        See INS_IsRDTSC() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_ret(self) -> Ins:
        """Filter RET instruction

        This call limits instrumentation RET instructions.

        See INS_IsRet() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_stack_read(self) -> Ins:
        """Filter instruction that read from the stack

        This call limits instrumentation to memory read instructions that are relative to the stack pointer or the frame pointer.
        This method will instrument any memory reads relative to ESP/RSP or EBP/RBP. This filtering may be misleading
        when EBP/RBP are used as general purpose registers and not as the frame pointer.

        See INS_IsStackRead() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_stack_write(self) -> Ins:
        """Filter instruction that write to the stack

        This call limits instrumentation to memory write instructions that are relative to the stack pointer or the frame pointer.
        This method will instrument any memory writes relative to ESP/RSP or EBP/RBP. This filtering may be misleading
        when EBP/RBP are used as general purpose registers and not as the frame pointer.

        See INS_IsStackWrite() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_standard_memop(self) -> Ins:
        """Filter instruction are considered standard memory operations

        This call limits instrumentation to standard memory operation instructions.
        Non-standard memory operations include operations that have unconventional meaning
        like vectorized memory reference or SCATTER/GATHER instructions or some of the XSAVE instructions.

        See INS_IsStandardMemop() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_string_op(self) -> Ins:
        """Filter string operation instructions

        This call limits instrumentation to string operation (may include REP prefix) instructions.

        See INS_IsStringop() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_sub(self) -> Ins:
        """Filter SUB instruction

        This call limits instrumentation to SUB instructions.

        See INS_IsSub() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_syscall(self) -> Ins:
        """Filter SYSCALL instruction

        This call limits instrumentation to SYSCALL instructions.

        See INS_IsSyscall() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_sysenter(self) -> Ins:
        """Filter SYSENTER instruction

        This call limits instrumentation to SYSENTER instructions.

        See INS_IsSysenter() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_sysret(self) -> Ins:
        """Filter SYSRET instruction

        This call limits instrumentation to SYSRET instructions.

        See INS_IsSysret() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_vgather(self) -> Ins:
        """Filter VGATHER instruction

        This call limits instrumentation to VGATHER instructions.

        See INS_IsVgather() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_vscatter(self) -> Ins:
        """Filter VSCATTER instruction

        This call limits instrumentation to VSCATTER instructions.

        See INS_IsVscatter() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_xbegin(self) -> Ins:
        """Filter XBEGIN instruction

        This call limits instrumentation to XBEGIN instructions.

        See INS_IsXbegin() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_xchg(self) -> Ins:
        """Filter XCHG instruction

        This call limits instrumentation to XCHG instructions.

        See INS_IsXchg() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def is_xend(self) -> Ins:
        """Filter XEND instruction

        This call limits instrumentation to XEND instructions.

        See INS_IsXend() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def memop_is_read(self, memopidx: int) -> Ins:
        """Filter instructions for which memory operand with index given by memopidx is a source.

        This call limits instrumentation to instructions for which operand with index given by memopidx is a source.


        See INS_MemoryOperandIsRead() in Pin API Reference

        Args:
            memopidx (int): The zero based memory operand index to check against.
                            If the given index is greater than INS_MemoryOperandCount() -1 
                            no filtering will be done.
        
        Raises:
            ValueError: memopidx is less than 0

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def memop_is_written(self, memopidx: int) -> Ins:
        """Filter instructions for which memory operand with index given by memopidx is a destination.

        This call limits instrumentation to instructions for which operand with index given by memopidx is a destination.


        See INS_MemoryOperandIsWritten() in Pin API Reference

        Args:
            memopidx (int): The zero based memory operand index to check against.
                            If the given index is greater than INS_MemoryOperandCount() -1 
                            no filtering will be done.
        
        Raises:
            ValueError: memopidx is less than 0

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def opcode(self, opcode: Opcode) -> Ins:
        """Filter instruction based on opcode class

        This call limits instrumentation to instructions of the given Opcode class.
        
        See INS_Opcode() in Pin API Reference

        Args:
            opcode (Opcode): The opcode class

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def operand_has_elements(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx has elements.

        This call limits instrumentation to instructions for which operand with index given by opidx has elements.


        See INS_OperandHasElements() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against. The operand can be either a memory operand 
                         or a register operand.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_address_generator(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is an address generator.

        This call limits instrumentation to instructions for which operand with index given by opidx 
        is an address generator.
        An address generator operand generates an address, but the address does not access memory 
        (e.g. load effective address instruction).

        See INS_OperandIsAddressGenerator() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_branch_displacement(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a branch displacement.

        This call limits instrumentation to instructions for which operand with index given by opidx 
        is a branch displacement.

        See INS_OperandIsBranchDisplacement() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_fixed_memop(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a memory operand 
           with a predefined base register that cannot be changed.

        This call limits instrumentation to instructions for which operand with index given by opidx 
        is a memory operand with a predefined base register that cannot be changed.
        An example is movs ds:(esi), es:(edi) which has two fixed operands.

        See INS_OperandIsFixedMemop() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_immediate(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is an immediate.

        This call limits instrumentation to instructions for which operand with index given by opidx is an immediate.

        See INS_OperandIsImmediate() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_implicit(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is implied by the opcode.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        implied by the opcode. An example would be the stack pointer modified by a push instruction.

        See INS_OperandIsImplicit() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_memory(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a memory operand.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a memory operand.
        Note that LEA operands are not considered memory operands.

        See INS_OperandIsMemory() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_reg(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a register.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a register.

        See INS_OperandIsReg() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_is_segment_reg(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a segment register.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a segment register.

        See INS_OperandIsSegmentReg() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_read(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a source.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a source (note that it may also be a destination).

        See INS_OperandRead() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_read_and_written(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is both a source and a destination.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        both a source and a destination.

        See INS_OperandReadAndWritten() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_read_only(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx a source
           and **not** a destination.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a source and **not** a destination.

        See INS_OperandReadOnly() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_written(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a destination.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a destination (note that it may also be a source).

        See INS_OperandWritten() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def operand_written_only(self, opidx: int) -> Ins:
        """Filter instructions for which operand with index given by opidx is a destination 
           and **not** a source.

        This call limits instrumentation to instructions for which operand with index given by opidx is 
        a destination and **not** a source.

        See INS_OperandWrittenOnly() in Pin API Reference

        Args:
            opidx (int): The zero based operand index to check against.
                         If the given index is greater than INS_OperandCount() -1 
                         no filtering will be done.
        
        Raises:
            ValueError: opidx is less than 0

        Returns:
            The Ins object (self)
        """

    def reg_is_read_operand(self, reg: Reg) -> Ins:
        """Filter instructions that have the given register as a read operand.

        This call limits instrumentation to instructions that have the given register as a read operand.

        See INS_RegRContain() in Pin API Reference

        Args:
            reg (Reg): The register to check against

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def reg_is_write_operand(self, reg: Reg) -> Ins:
        """Filter instructions that have the given register as a write operand.

        This call limits instrumentation to instructions that have the given register as a write operand.

        See INS_RegWContain() in Pin API Reference

        Args:
            reg (Reg): The register to check against

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def seg_prefix_is_memory_read(self) -> Ins:
        """Filter memory read instructions with seg prefix

        This call limits instrumentation to seg prefixed memory read instructions.

        See INS_SegPrefixIsMemoryRead() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self

    def seg_prefix_is_memory_write(self) -> Ins:
        """Filter memory write instructions with seg prefix

        This call limits instrumentation to seg prefixed memory write instructions.

        See INS_SegPrefixIsMemoryWrite() in Pin API Reference

        Returns:
            The Ins object (self)
        """

        raise NotImplementedError()

        return self
