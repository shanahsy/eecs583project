#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static bool isMemAccess(const Instruction &I) {
  return isa<LoadInst>(&I) || isa<StoreInst>(&I);
}

struct CacheOptPass : public PassInfoMixin<CacheOptPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    // outs() << "[CacheOptPass::run] visiting module: " << M.getName() << "\n";

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      // outs() << "[CacheOptPass] visiting function " << F.getName() << "\n";

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

          outs() << F.getName() << ","
                 << (isa<LoadInst>(&I) ? "load" : "store") << ","
                 << FileName << "," << Line << "\n";
        }
      }
    }

    return PreservedAnalyses::all();
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
            // outs() << "[CacheOptPass] registering *module* pipeline 'cache-opt'\n";
            MPM.addPass(CacheOptPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}
