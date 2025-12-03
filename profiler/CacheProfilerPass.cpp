// profiler/CacheProfilerPass.cpp
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// Simple skeleton pass: currently just prints the function name.
/// You’ll later replace this with your load/store instrumentation.
class CacheProfilerPass : public PassInfoMixin<CacheProfilerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    // For now, just sanity-check that the pass runs
    errs() << "[CacheProfilerPass] Visiting function: " << F.getName() << "\n";

    // Currently we don't modify anything, so we preserve all analyses
    return PreservedAnalyses::all();
  }
};

} // end anonymous namespace

// === Pass registration for the new pass manager ===
// This allows: opt -load-pass-plugin=./CacheProfilerPass.dylib -passes="function(cache-profiler)" ...
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION,
      "CacheProfilerPass",
      "0.1",
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name,
               FunctionPassManager &FPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "cache-profiler") {
                FPM.addPass(CacheProfilerPass());
                return true;
              }
              return false;
            });
      }};
}
