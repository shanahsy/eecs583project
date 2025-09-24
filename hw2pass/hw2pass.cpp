//===-- Frequent Path Loop Invariant Code Motion Pass --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// CSE583 F25 - This pass can be used as a template for your FPLICM homework
//               assignment.
//               The passes get registered as "fplicm-correctness" and
//               "fplicm-performance".
//
//
////===-------------------------------------------------------------------===//
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

/* *******Implementation Starts Here******* */
// You can include more Header files here
/* *******Implementation Ends Here******* */

using namespace llvm;

namespace {
  struct HW2CorrectnessPass : public PassInfoMixin<HW2CorrectnessPass> {

    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
      llvm::BlockFrequencyAnalysis::Result &bfi = FAM.getResult<BlockFrequencyAnalysis>(F);
      llvm::BranchProbabilityAnalysis::Result &bpi = FAM.getResult<BranchProbabilityAnalysis>(F);
      llvm::LoopAnalysis::Result &li = FAM.getResult<LoopAnalysis>(F);
      /* *******Implementation Starts Here******* */
      // Your core logic should reside here.
      // LoopInPreorder gets you every loop inside function even if it's nested
      // for (auto &loop : li.getLoopsInPreorder()) {
      //   // Find most likely path
      //   // Find loopheader, then loop through each block and finds their freq?? and somehow use that to figure out which one is the most freq path??
      //   // Find loopheader
      //   // At each block, if it's unconditional then add it to the path. if it's a conditional block, check the branch probabilities and go down the more likely path
      //   // Keep going until you exit loop/revisit a block (thus looping back
      //   BasicBlock *currentBB = loop->getHeader();
      //   SmallVector<BasicBlock*, 8> likelyPath;
      //   SmallSet<BasicBlock*, 8> visited; // To track visited blocks
      //   while (currentBB && visited.count(currentBB) == 0 && loop->contains(currentBB)) {
      //     visited.insert(currentBB);
      //     likelyPath.push_back(currentBB);
      //     // Check if block ends with branch insturction/is conditional
      //     Instruction *termInstr = currentBB->getTerminator();
      //     // check if is branch instruction
      //     // Try to convert termInstr into a branch, if it's a branch instr it'll return sumn if not it'll return a nullptr
      //     auto *br = dyn_cast<BranchInst>(termInstr);
      //     // doesnt accont for switches, invokes, indirectbrs...
      //     // looks like we have to choose the >=80% branch so??? somehow we do this??
      //     if (br) {

      //     } else {
      //       // Not a branch instruction, so ew can just go to the next block normally
      //     }
      //   } 
      // }

      // This is going through each function
      // This is going through each Basic Block in the function F
      // for (auto &BB : F) {
        // For each instruction in the BB
        // for (auto &I : BB) {
        //   // check if instr is almost invariant (never changing)
        //   // never redefined ig

        // }
        // go through each loop in function
        // identify most likely path
        // identify load instructions on likely path that are "almomst invariant" and hoist them
        // Create and insert repair code i nthe infrequent paths to handle mis-speculation (output of original code and optimized code should be identical)

      // }

      /* *******Implementation Ends Here******* */
      // Your pass is modifying the source code. Figure out which analyses
      // are preserved and only return those, not all.
      return PreservedAnalyses::all();
    }
  };
  struct HW2PerformancePass : public PassInfoMixin<HW2PerformancePass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
      llvm::BlockFrequencyAnalysis::Result &bfi = FAM.getResult<BlockFrequencyAnalysis>(F);
      llvm::BranchProbabilityAnalysis::Result &bpi = FAM.getResult<BranchProbabilityAnalysis>(F);
      llvm::LoopAnalysis::Result &li = FAM.getResult<LoopAnalysis>(F);
      /* *******Implementation Starts Here******* */
      // This is a bonus. You do not need to attempt this to receive full credit.
      /* *******Implementation Ends Here******* */

      // Your pass is modifying the source code. Figure out which analyses
      // are preserved and only return those, not all.
      return PreservedAnalyses::all();
    }
  };
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "HW2Pass", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
        ArrayRef<PassBuilder::PipelineElement>) {
          if(Name == "fplicm-correctness"){
            FPM.addPass(HW2CorrectnessPass());
            return true;
          }
          if(Name == "fplicm-performance"){
            FPM.addPass(HW2PerformancePass());
            return true;
          }
          return false;
        }
      );
    }
  };
}
