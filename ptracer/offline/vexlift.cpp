// vexlift.cpp -- see vexlift.h. Uses pyvex's C shim (vex_lift) around LibVEX_Translate.
#include "vexlift.h"
#include <cstring>
#include <stdexcept>
extern "C" {
#include <pyvex.h>
}

VexLifter::VexLifter() {
    static bool inited = false;
    if (!inited) { if (!vex_init()) throw std::runtime_error("vex_init failed"); inited = true; }
    log_level = 40; // python logging.ERROR
    memset(&ai_, 0, sizeof ai_);
    LibVEX_default_VexArchInfo(&ai_);
    ai_.hwcaps = VEX_HWCAPS_AMD64_SSE3 | VEX_HWCAPS_AMD64_CX16 | VEX_HWCAPS_AMD64_LZCNT | VEX_HWCAPS_AMD64_AVX |
                 VEX_HWCAPS_AMD64_RDTSCP | VEX_HWCAPS_AMD64_BMI | VEX_HWCAPS_AMD64_AVX2;
    ai_.endness = VexEndnessLE;
    ai_.hwcache_info.caches = nullptr; ai_.hwcache_info.num_caches = 0; ai_.hwcache_info.num_levels = 0;
    ai_.hwcache_info.icaches_maintain_coherence = True;
    ai_.x86_cr0 = 0xffffffff;
}

const char* irop_name(IROp op) { return "op"; }

static void const_bytes(IRNodeC& n, const IRConst* c) {
    switch (c->tag) {
        case Ico_U1: n.c[0] = c->Ico.U1 ? 1 : 0; n.size = 1; break;
        case Ico_U8: n.c[0] = c->Ico.U8; n.size = 1; break;
        case Ico_U16: n.c[0] = c->Ico.U16; n.size = 2; break;
        case Ico_U32: n.c[0] = c->Ico.U32; n.size = 4; break;
        case Ico_U64: n.c[0] = c->Ico.U64; n.size = 8; break;
        case Ico_F32i: n.c[0] = c->Ico.F32i; n.size = 4; break;
        case Ico_F64i: n.c[0] = c->Ico.F64i; n.size = 8; break;
        case Ico_F32: { float f = c->Ico.F32; uint32_t u; memcpy(&u, &f, 4); n.c[0] = u; n.size = 4; break; }
        case Ico_F64: { double d = c->Ico.F64; memcpy(&n.c[0], &d, 8); n.size = 8; break; }
        case Ico_V128: { // 16-bit mask: each bit selects 0x00 or 0xFF for a byte
            uint16_t m = c->Ico.V128; for (int i = 0; i < 16; i++) if (m & (1 << i)) n.c[i / 8] |= (uint64_t)0xff << (8 * (i % 8)); n.size = 16; break; }
        case Ico_V256: { uint32_t m = c->Ico.V256; for (int i = 0; i < 32; i++) if (m & (1u << i)) n.c[i / 8] |= (uint64_t)0xff << (8 * (i % 8)); n.size = 32; break; }
        default: n.k = IRNodeC::UNK; break;
    }
}

int VexLifter::compile_expr(IRBlockC& b, const IRExpr* e) {
    IRNodeC n;
    switch (e->tag) {
        case Iex_Const: n.k = IRNodeC::CONST; const_bytes(n, e->Iex.Const.con); n.ty = Ity_INVALID; break;
        case Iex_Get: n.k = IRNodeC::GET; n.off = e->Iex.Get.offset; n.ty = e->Iex.Get.ty; n.size = sizeofIRType(e->Iex.Get.ty); break;
        case Iex_GetI: n.k = IRNodeC::GETI; n.ty = e->Iex.GetI.descr->elemTy; n.size = sizeofIRType(n.ty); n.a[0] = compile_expr(b, e->Iex.GetI.ix); break;
        case Iex_RdTmp: n.k = IRNodeC::RDTMP; n.off = e->Iex.RdTmp.tmp; n.ty = b.tmpty[n.off]; break;
        case Iex_Unop: n.k = IRNodeC::UNOP; n.op = e->Iex.Unop.op; n.a[0] = compile_expr(b, e->Iex.Unop.arg); break;
        case Iex_Binop: n.k = IRNodeC::BINOP; n.op = e->Iex.Binop.op; n.a[0] = compile_expr(b, e->Iex.Binop.arg1); n.a[1] = compile_expr(b, e->Iex.Binop.arg2); break;
        case Iex_Triop: n.k = IRNodeC::TRIOP; n.op = e->Iex.Triop.details->op; n.a[0] = compile_expr(b, e->Iex.Triop.details->arg1); n.a[1] = compile_expr(b, e->Iex.Triop.details->arg2); n.a[2] = compile_expr(b, e->Iex.Triop.details->arg3); break;
        case Iex_Qop: n.k = IRNodeC::QOP; n.op = e->Iex.Qop.details->op; n.a[0] = compile_expr(b, e->Iex.Qop.details->arg1); n.a[1] = compile_expr(b, e->Iex.Qop.details->arg2); n.a[2] = compile_expr(b, e->Iex.Qop.details->arg3); n.a[3] = compile_expr(b, e->Iex.Qop.details->arg4); break;
        case Iex_Load: n.k = IRNodeC::LOAD; n.ty = e->Iex.Load.ty; n.size = sizeofIRType(n.ty); n.a[0] = compile_expr(b, e->Iex.Load.addr); break;
        case Iex_ITE: n.k = IRNodeC::ITE; n.a[0] = compile_expr(b, e->Iex.ITE.cond); n.a[1] = compile_expr(b, e->Iex.ITE.iftrue); n.a[2] = compile_expr(b, e->Iex.ITE.iffalse); break;
        case Iex_CCall: n.k = IRNodeC::CCALL; n.ty = e->Iex.CCall.retty; n.ccname = e->Iex.CCall.cee->name;
            for (int i = 0; e->Iex.CCall.args[i]; i++) n.cargs.push_back(compile_expr(b, e->Iex.CCall.args[i])); break;
        default: n.k = IRNodeC::UNK; break;
    }
    b.nodes.push_back(std::move(n));
    return (int)b.nodes.size() - 1;
}

std::unique_ptr<IRBlockC> VexLifter::lift_one(uint64_t addr, const uint8_t* bytes, size_t nbytes) {
    auto b = std::make_unique<IRBlockC>();
    b->addr = addr;
    uint8_t buf[32]; memset(buf, 0x90, sizeof buf);   // pad so VEX never reads past the instruction
    size_t nb = nbytes > 16 ? 16 : nbytes; memcpy(buf, bytes, nb);
    clear_log();
    VEXLiftResult* r = vex_lift(VexArchAMD64, ai_, buf, addr, 1 /*max_insns*/, (unsigned)nb, 1 /*opt_level*/, 0, 1, 1, 0, 0, 0,
                                VexRegUpdLdAllregsAtEachInsn, 0);
    if (!r || !r->irsb || r->size == 0) { b->err = msg_buffer ? std::string(msg_buffer, msg_current_size) : "lift failed"; return b; }
    IRSB* sb = r->irsb;
    b->len = r->size;
    b->tmpty.assign(sb->tyenv->types, sb->tyenv->types + sb->tyenv->types_used);
    for (int i = 0; i < sb->stmts_used; i++) {
        const IRStmt* s = sb->stmts[i];
        IRStmtC c;
        switch (s->tag) {
            case Ist_NoOp: case Ist_AbiHint: c.k = IRStmtC::NOP; break;
            case Ist_IMark: c.k = IRStmtC::IMARK; c.addr = s->Ist.IMark.addr; c.len = s->Ist.IMark.len; break;
            case Ist_Put: c.k = IRStmtC::PUT; c.off = s->Ist.Put.offset; c.e[0] = compile_expr(*b, s->Ist.Put.data);
                c.size = sizeofIRType(typeOfIRExpr(sb->tyenv, s->Ist.Put.data)); break;
            case Ist_PutI: c.k = IRStmtC::PUTI; c.e[0] = compile_expr(*b, s->Ist.PutI.details->data); c.e[1] = compile_expr(*b, s->Ist.PutI.details->ix);
                c.off = s->Ist.PutI.details->descr->base; c.size = s->Ist.PutI.details->descr->nElems * sizeofIRType(s->Ist.PutI.details->descr->elemTy); break;
            case Ist_WrTmp: c.k = IRStmtC::WRTMP; c.tmp = s->Ist.WrTmp.tmp; c.e[0] = compile_expr(*b, s->Ist.WrTmp.data); break;
            case Ist_Store: c.k = IRStmtC::STORE; c.e[0] = compile_expr(*b, s->Ist.Store.addr); c.e[1] = compile_expr(*b, s->Ist.Store.data);
                c.size = sizeofIRType(typeOfIRExpr(sb->tyenv, s->Ist.Store.data)); break;
            case Ist_StoreG: { const IRStoreG* d = s->Ist.StoreG.details; c.k = IRStmtC::STOREG; c.e[0] = compile_expr(*b, d->addr); c.e[1] = compile_expr(*b, d->data); c.e[2] = compile_expr(*b, d->guard);
                c.size = sizeofIRType(typeOfIRExpr(sb->tyenv, d->data)); break; }
            case Ist_LoadG: { const IRLoadG* d = s->Ist.LoadG.details; c.k = IRStmtC::LOADG; c.tmp = d->dst; c.cvt = d->cvt; c.e[0] = compile_expr(*b, d->addr); c.e[1] = compile_expr(*b, d->alt); c.e[2] = compile_expr(*b, d->guard);
                switch (d->cvt) { case ILGop_IdentV128: c.size = 16; break; case ILGop_Ident64: c.size = 8; break; case ILGop_Ident32: c.size = 4; break; case ILGop_16Uto32: case ILGop_16Sto32: c.size = 2; break; default: c.size = 1; } break; }
            case Ist_CAS: { const IRCAS* d = s->Ist.CAS.details; c.k = IRStmtC::CAS; c.tmp = d->oldLo; c.tmp2 = d->oldHi; c.e[0] = compile_expr(*b, d->addr); c.e[1] = compile_expr(*b, d->expdLo); c.e[2] = compile_expr(*b, d->dataLo);
                if (d->expdHi) { c.e[3] = compile_expr(*b, d->expdHi); c.e[4] = compile_expr(*b, d->dataHi); }
                c.size = sizeofIRType(typeOfIRExpr(sb->tyenv, d->dataLo)) * (d->expdHi ? 2 : 1); break; }
            case Ist_LLSC: c.k = IRStmtC::LLSC; c.tmp = s->Ist.LLSC.result; c.e[0] = compile_expr(*b, s->Ist.LLSC.addr);
                if (s->Ist.LLSC.storedata) { c.e[1] = compile_expr(*b, s->Ist.LLSC.storedata); c.size = sizeofIRType(typeOfIRExpr(sb->tyenv, s->Ist.LLSC.storedata)); }
                else c.size = sizeofIRType(b->tmpty[c.tmp]); break;
            case Ist_Dirty: { const IRDirty* d = s->Ist.Dirty.details; c.k = IRStmtC::DIRTY; c.tmp = d->tmp; c.e[0] = compile_expr(*b, d->guard); c.mfx = d->mFx; c.msize = d->mSize;
                if (d->mAddr) c.e[1] = compile_expr(*b, d->mAddr); break; }
            case Ist_MBE: c.k = IRStmtC::MBE; break;
            case Ist_Exit: c.k = IRStmtC::EXIT; c.e[0] = compile_expr(*b, s->Ist.Exit.guard); c.jk = s->Ist.Exit.jk; c.dst = s->Ist.Exit.dst->Ico.U64; break;
            default: c.k = IRStmtC::NOP; break;
        }
        b->stmts.push_back(std::move(c));
    }
    b->jk = sb->jumpkind;
    b->next = compile_expr(*b, sb->next);
    b->ok = true;
    return b;
}
