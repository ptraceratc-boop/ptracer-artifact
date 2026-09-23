/*
 * S-Tracer instrumentation pass -- SPINDLE-PLUS variant (pass name "stracerplus").
 * Identical instrumentation logic to the base Spindle S-Tracer; the ONLY difference
 * in Spindle-Plus is the RUNTIME library (stracer_lib_mt.c = thread-safe), so this
 * pass can instrument multithreaded programs (memcached). The static analysis and
 * the computable/non-computable classification are byte-for-byte the same; there is
 * no algorithmic change.
 *
 * Reuses Spindle's UNMODIFIED common static analysis (MTS.cpp: memory-access
 * skeleton, dependence trees, loop induction-variable detection) and emits a
 * memory-access *trace* the way the ATC'18 paper's S-Tracer does, instead of the
 * S-Detector bounds checks. The public Spindle repo does not ship S-Tracer; this
 * is a faithful reimplementation of its runtime path for an overhead comparison.
 *
 * Decision (mirrors SDetector::analysisMemAccess, Spindle's computable test):
 *   - mustTakeFullInstrumentation()                 -> NON-COMPUTABLE -> per-access record
 *   - isDependenciesConstant()                      -> COMPUTABLE     -> no per-access record
 *   - deps.size()==1 && that dep is a loop indvar   -> COMPUTABLE     (single-index loop)
 *   - deps.size()>1 && all deps are loop indvars    -> COMPUTABLE     (nested affine)
 *   - otherwise                                     -> NON-COMPUTABLE -> per-access record
 * Computable accesses are reconstructable offline from the skeleton + loop seeds,
 * so they incur NO per-access runtime cost -- this is Spindle's contribution and
 * what makes its tracing overhead low on the narrow loop class it handles.
 *
 * Registered as pass name "stracer" for opt's -passes=.
 * Does NOT run S-Detector (avoids its instrumentForFree bug, and S-Detector is a
 * different tool).
 */
#include "MTS.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

namespace {

/* meta bit0 = is_write, bits 1..16 = access size in bytes */
static inline uint64_t accessMeta(bool isWrite, uint64_t size) {
  return (isWrite ? 1ULL : 0ULL) | ((size & 0xFFFFULL) << 1);
}

class STracerPass : public PassInfoMixin<STracerPass> {
  // Counters reported on stderr for the harness / sanity check.
  unsigned nFull = 0, nComputable = 0, nLoops = 0, nBase = 0;

public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << "[stracer] module: " << M.getModuleIdentifier() << "\n";
    auto MMC = MyModuleContext(M);
    for (auto &F : M) {
      if (F.isDeclaration())
        continue;
      analysisFunction(F, MMC);
    }
    for (auto MF : MMC)
      instrumentFunction(MF);

    injectMainHooks(M);

    errs() << "[stracer] static summary: non-computable(full)=" << nFull
           << " computable(pruned)=" << nComputable << " loops-seeded=" << nLoops
           << " base-addrs=" << nBase << "\n";
    if (nFull + nComputable > 0)
      errs() << "[stracer] computable fraction = "
             << (100.0 * nComputable / (nFull + nComputable)) << "%\n";
    return PreservedAnalyses::none();
  }

private:
  // ---- Replicates Spindle.cpp's per-function MAS + loop construction. --------
  void analysisFunction(Function &F, MyModuleContext &MMC) {
    auto MF = new MyFunction(F, MMC);
    PassBuilder PB;
    FunctionAnalysisManager FAM;
    PB.registerFunctionAnalyses(FAM);
    LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);
    std::vector<Loop *> workspace;
    for (auto it : LI)
      workspace.push_back(it);
    while (!workspace.empty()) {
      auto L = workspace.back();
      workspace.pop_back();
      MF->addLoop(*L);
      for (auto sl : *L)
        workspace.push_back(sl);
    }
    auto &MLs = MF->getLoops();
    for (auto MBB : *MF) {
      auto L = LI.getLoopFor(&MBB->getBB());
      if (!L)
        continue;
      for (auto ML : MLs)
        if (L == &ML->getLoop()) {
          MBB->setLoopID(ML->getID());
          break;
        }
    }
    MF->loopAnalysis();
  }

  // ---- Spindle's computable test (mirror of SDetector::analysisMemAccess). ---
  bool isComputable(MTSMemAccess *mn) {
    if (mn->mustTakeFullInstrumentation())
      return false;
    if (mn->isDependenciesConstant())
      return true;
    auto deps = mn->getDependencies();
    if (deps.size() == 1)
      return mn->tryToGetMyLoopFor(deps[0]) != nullptr;
    for (auto dep : deps)
      if (!mn->isDependencyLoopIndvar(dep))
        return false;
    return true; // all deps are loop induction variables (nested affine)
  }

  void instrumentFunction(MyFunction *MF) {
    auto &F = MF->getFunction();
    auto M = F.getParent();
    auto &Ctx = M->getContext();
    const DataLayout &DL = M->getDataLayout();
    auto voidTy = Type::getVoidTy(Ctx);
    auto i64Ty = Type::getInt64Ty(Ctx);
    auto i8pTy = PointerType::getUnqual(Ctx);

    auto straceAddr = M->getOrInsertFunction(
        "__strace_addr", FunctionType::get(voidTy, {i8pTy, i64Ty}, false));
    auto straceBase = M->getOrInsertFunction(
        "__strace_base", FunctionType::get(voidTy, {i8pTy, i64Ty}, false));
    auto loopEnter = M->getOrInsertFunction(
        "__strace_loop_enter", FunctionType::get(voidTy, {i64Ty}, false));
    auto loopExit = M->getOrInsertFunction(
        "__strace_loop_exit", FunctionType::get(voidTy, {i64Ty}, false));

    std::set<MyLoop *> loopsToSeed;
    std::set<Value *> basesRecorded;

    for (auto BB : *MF) {
      for (auto node : *BB) {
        auto kind = node->getKind();
        if (kind != MTSNode::LoadKind && kind != MTSNode::StoreKind)
          continue;
        auto mn = dyn_cast<MTSMemAccess>(node);
        auto inst = mn->getInstruction();
        bool isWrite = (kind == MTSNode::StoreKind);
        Value *ptr;
        Type *valTy;
        if (isWrite) {
          auto si = dyn_cast<StoreInst>(inst);
          ptr = si->getPointerOperand();
          valTy = si->getValueOperand()->getType();
        } else {
          auto li = dyn_cast<LoadInst>(inst);
          ptr = li->getPointerOperand();
          valTy = li->getType();
        }
        uint64_t size = DL.getTypeStoreSize(valTy).getFixedValue();

        if (!isComputable(mn)) {
          // Non-computable: record the dynamic address on every execution.
          IRBuilder<> B(inst);
          Value *p = B.CreateBitOrPointerCast(ptr, i8pTy);
          B.CreateCall(straceAddr,
                       {p, ConstantInt::get(i64Ty, accessMeta(isWrite, size))});
          ++nFull;
        } else {
          ++nComputable;
          // Mark enclosing loop(s) so their seeds get recorded once per entry.
          for (auto dep : mn->getDependencies())
            if (auto ml = mn->tryToGetMyLoopFor(dep))
              loopsToSeed.insert(ml);
          // Record the base address once (needed offline to materialize the
          // computable addresses); does not scale with iteration count.
          recordBase(mn->getBasement(), basesRecorded, straceBase, i8pTy, i64Ty);
        }
      }
    }

    // Loop seeds: one record per loop entry/exit (O(loop executions), not O(accesses)).
    for (auto ml : loopsToSeed)
      seedLoop(ml, loopEnter, loopExit, i64Ty);
  }

  void recordBase(Value *base, std::set<Value *> &recorded, FunctionCallee fn,
                  Type *i8pTy, Type *i64Ty) {
    if (!base || recorded.count(base))
      return;
    auto inst = dyn_cast<Instruction>(base);
    if (!inst)
      return; // global/arg: address known statically, no runtime record needed
    if (inst->isTerminator())
      return;
    Instruction *ip = inst->getNextNode();
    while (ip && PHINode::classof(ip))
      ip = ip->getNextNode();
    if (!ip)
      return;
    IRBuilder<> B(ip);
    Value *p = B.CreateBitOrPointerCast(base, i8pTy);
    B.CreateCall(fn, {p, ConstantInt::get(i64Ty, 0)});
    recorded.insert(base);
    ++nBase;
  }

  void seedLoop(MyLoop *ml, FunctionCallee enterFn, FunctionCallee exitFn,
                Type *i64Ty) {
    auto preheader = ml->getLoopPreheader();
    auto header = ml->getHeader();
    if (!preheader || !header)
      return;
    // Start value = induction PHI's incoming value from the preheader.
    Value *startVal = ConstantInt::get(i64Ty, 0);
    for (auto it = header->begin(); it != header->end() && PHINode::classof(&*it);
         ++it) {
      auto phi = dyn_cast<PHINode>(&*it);
      if (!phi->getType()->isIntegerTy())
        continue;
      if (auto v = phi->getIncomingValueForBlock(preheader)) {
        IRBuilder<> B(preheader->getTerminator());
        startVal = B.CreateZExtOrTrunc(v, i64Ty);
        break;
      }
    }
    {
      IRBuilder<> B(preheader->getTerminator());
      B.CreateCall(enterFn, {startVal});
    }
    // Exit record: one marker per loop exit (trip count is recoverable offline).
    for (auto exitBB : ml->getExitBlocks()) {
      auto ip = &*exitBB->getFirstInsertionPt();
      IRBuilder<> B(ip);
      B.CreateCall(exitFn, {ConstantInt::get(i64Ty, 0)});
    }
    ++nLoops;
  }

  // ---- init/fini hooks around main (mirrors Spindle.cpp). -------------------
  void injectMainHooks(Module &M) {
    auto &Ctx = M.getContext();
    auto voidTy = Type::getVoidTy(Ctx);
    auto fnTy = FunctionType::get(voidTy, false);
    auto initFn = M.getOrInsertFunction("__init_main", fnTy);
    auto finiFn = M.getOrInsertFunction("__fini_main", fnTy);
    auto main = M.getFunction("main");
    if (main && !main->isDeclaration()) {
      IRBuilder<> B(&*main->begin()->getFirstInsertionPt());
      B.CreateCall(initFn);
      for (auto &BB : *main)
        if (auto ri = dyn_cast<ReturnInst>(BB.getTerminator())) {
          IRBuilder<> RB(ri);
          RB.CreateCall(finiFn);
        }
    }
    // Also flush before any exit()/_exit() so abnormal terminations still flush.
    for (auto &F : M)
      for (auto &BB : F)
        for (auto &I : BB)
          if (auto ci = dyn_cast<CallInst>(&I))
            if (auto cf = ci->getCalledFunction()) {
              auto n = cf->getName();
              if (n == "exit" || n == "_exit" || n == "_Exit") {
                IRBuilder<> B(ci);
                B.CreateCall(finiFn);
              }
            }
  }
};

} // namespace

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "STracerPlusPass", "v0.1", [](PassBuilder &PB) {
            // Explicit pipeline use: opt -passes=stracerplus
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM, ...) {
                  if (Name == "stracerplus") {
                    MPM.addPass(STracerPass());
                    return true;
                  }
                  return false;
                });
            // Auto-run during a normal clang build via -fpass-plugin, but ONLY when
            // STRACER_AUTO is set in the environment -- so it does not double-run
            // when invoked explicitly through opt -passes=. Used for the CPython
            // build (instrument every TU as it compiles, no whole-program bitcode).
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                  if (getenv("STRACER_AUTO"))
                    MPM.addPass(STracerPass());
                });
          }};
}
