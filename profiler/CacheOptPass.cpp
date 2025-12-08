#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static bool isMemAccess(const Instruction &I) {
  return isa<LoadInst>(&I) || isa<StoreInst>(&I);
}

struct CacheOptPass : public PassInfoMixin<CacheOptPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    LLVMContext &Ctx = M.getContext();

    // Types
    Type *VoidTy  = Type::getVoidTy(Ctx);
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    Type *Int8Ty  = Type::getInt8Ty(Ctx);
    // older LLVM: no Type::getInt8PtrTy, so use PointerType
    PointerType *Int8PtrTy = PointerType::getUnqual(Int8Ty);

    // void cacheopt_log(int id, void *addr, int isLoad);
    FunctionCallee LogFn = M.getOrInsertFunction(
        "cacheopt_log",
        VoidTy,
        Int32Ty,
        Int8PtrTy,
        Int32Ty
    );

    int NextID = 0;

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (!isMemAccess(I))
            continue;

          DebugLoc DL = I.getDebugLoc();
          if (!DL)
            continue;

          const DILocation *Loc = DL.get();
          if (!Loc)
            continue;

          StringRef FileName = Loc->getFilename();
          unsigned Line      = Loc->getLine();
          bool IsLoad        = isa<LoadInst>(&I);

          int InstID = NextID++;

          // ---- static mapping: ID,Func,kind,File,Line ----
          outs() << InstID << ","
                 << F.getName() << ","
                 << (IsLoad ? "load" : "store") << ","
                 << FileName << ","
                 << Line << "\n";

          // ---- instrumentation: call cacheopt_log ----
          Value *Ptr = nullptr;
          if (auto *LI = dyn_cast<LoadInst>(&I)) {
            Ptr = LI->getPointerOperand();
          } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
            Ptr = SI->getPointerOperand();
          }

          if (!Ptr)
            continue;

          IRBuilder<> B(&I); // insert *before* instruction

          Value *PtrCast   = B.CreateBitCast(Ptr, Int8PtrTy);
          Value *IDConst   = ConstantInt::get(Int32Ty, InstID);
          Value *IsLoadVal = ConstantInt::get(Int32Ty, IsLoad ? 1 : 0);

          B.CreateCall(LogFn, {IDConst, PtrCast, IsLoadVal});
        }
      }
    }

    return PreservedAnalyses::none();
  }
};

extern "C" PassPluginLibraryInfo
LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION,
    "CacheOptPass",
    "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "cache-opt") {
            MPM.addPass(CacheOptPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}
