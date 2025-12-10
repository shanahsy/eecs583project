#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm> // for std::remove
#include <cstdint>
#include <fstream>
#include <llvm/MC/MCFragment.h>
#include <map>
#include <regex>
#include <sstream>
#include <string>

using namespace llvm;
typedef std::pair<std::string, int> FileLinePair;

static cl::opt<std::string> CacheCGFile("cache-cg-file",
                                        cl::desc("Path to cg_annotate output"),
                                        cl::init(""));

static cl::opt<uint64_t> MissThreshold(
    "cache-miss-threshold",
    cl::desc("Total data cache misses (D1mr+DLmr+D1mw+DLmw) needed to prefetch"),
    cl::init(1000)); // Subject to change

// Constants
const std::string AutoAnnotatedPrefix = "-- Auto-annotated source:";
const std::string EndAnnotatedBlock =
    "--------------------------------------------------------------------------"
    "------";

/**
Metrics from Cachegrind for that specific line.
 */
struct CacheMetrics {
  uint64_t Dr = 0;
  uint64_t D1mr = 0;
  uint64_t DLmr = 0;
  uint64_t Dw = 0;
  uint64_t D1mw = 0;
  uint64_t DLmw = 0;
};

struct ParseCachegrindPass : public PassInfoMixin<ParseCachegrindPass> {

  /// Maps filenames & line numbers to the cachegrind information
  std::map<FileLinePair, CacheMetrics> lineMetrics;

  bool isHotLine(const FileLinePair &fl) {
    auto it = lineMetrics.find(fl);
    if (it == lineMetrics.end())
      return false;

    const CacheMetrics &cm = it->second;
    uint64_t totalMisses = cm.D1mr + cm.DLmr + cm.D1mw + cm.DLmw;
    return totalMisses >= MissThreshold;
  }

    bool insertPrefetch(Instruction *I) {
    IRBuilder<> Builder(I);

    Value *Addr = nullptr;

    if (auto *LI = dyn_cast<LoadInst>(I)) {
      Addr = LI->getPointerOperand();
    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
      Addr = SI->getPointerOperand();
    } else {
      return false; // not a load/store
    }

    Module *M = I->getModule();
    LLVMContext &Ctx = M->getContext();

    // Cast to i8* as required by llvm.prefetch
    Type *I8PtrTy = Type::getInt8Ty(Ctx)->getPointerTo();
    Value *AddrI8 = Builder.CreateBitCast(Addr, I8PtrTy);

    // declare void @llvm.prefetch.p0(i8* addr, i32 rw, i32 locality, i32 cache_type)
    Function *PrefetchFn =
        Intrinsic::getDeclaration(M, Intrinsic::prefetch, {I8PtrTy});

    // rw: 0 = read, 1 = write
    // locality: 0 (none) .. 3 (high)
    // cache_type: 1 = data cache
    Value *RW = Builder.getInt32(0);        // assume read prefetch for now
    Value *Locality = Builder.getInt32(3);  // guess high locality
    Value *CacheType = Builder.getInt32(1); // data cache

    Builder.CreateCall(PrefetchFn, {AddrI8, RW, Locality, CacheType});
    return true;
  }


  bool parseInputFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs) {
      errs() << "Failed to open cachegrind file: " << path << "\n";
      return false;
    }

    std::string line;
    std::string currentFile;
    int lineNumber = 0;
    while (std::getline(ifs, line)) {
      // if line is the start of the annotated cache info (code block)
      if (line.rfind(AutoAnnotatedPrefix, 0) == 0) {
        // get filename after colon and trim excess spaces
        auto filename = line.substr(AutoAnnotatedPrefix.size());
        currentFile =
            std::regex_replace(filename, std::regex("^ +| +$|( ) +"), "$1");
        lineNumber = 0;
        std::getline(ifs, line); // skips first ------ block
        // now inside a code block, reset line number and start looking at next
        // lines
        continue;
      }

      // if we're at the end of an annotated block
      // added line number check since beginning block after filename has the
      // same dashes
      if (!currentFile.empty() && line.rfind(EndAnnotatedBlock, 0) == 0 &&
          lineNumber != 0) {
        currentFile.clear();
        continue;
      }

      // if we're not in an annotated block, just skip to next line
      if (currentFile.empty()) {
        continue;
      }

      // skip the header row: "Ir I1mr ILmr..."
      if (line.rfind("Ir", 0) == 0) {
        continue;
      }

      // handle blank lines, dont inc line count
      if (line.empty()) {
        continue;
      }

      // at this point we know we're inside of a an annotated block
      // left trim the spaces
      auto trimmedLine = std::regex_replace(line, std::regex("^ +"), "$1");

      // handle line skips ex "-- line 39 --------------"
      if (trimmedLine.rfind("-- line", 0) == 0) {
        // after "-- line " comes the number
        std::stringstream ss(trimmedLine.substr(std::strlen("-- line")));
        int ln = 0;
        ss >> ln;
        lineNumber = ln - 1;
        continue;
      }

      ++lineNumber;

      // just double check that a line entry is either a digit or .
      // otherwise skip to be safe
      char firstField = trimmedLine[0];
      if (trimmedLine.empty() ||
          !std::isdigit(static_cast<unsigned char>(trimmedLine[0]))) {
        continue;
      }

      std::stringstream ss(trimmedLine);
      uint64_t fields[6];
      std::string temp;
      // skip first three fields (Ir I1mr ILmr)
      ss >> temp >> temp >> temp;
      // read Dr, D1mr, DLmr, Dw, D1mw, DLmw
      for (int i = 0; i < 6; ++i) {
        if (!(ss >> temp)) {
          // not enough tokens, treat remaining fields as zero.
          fields[i] = 0;
          continue;
        }

        // remove commas from numbers like "7,004"
        temp.erase(std::remove(temp.begin(), temp.end(), ','), temp.end());

        // non numerics treat as 0
        if (temp.empty() ||
            !std::isdigit(static_cast<unsigned char>(temp[0]))) {
          fields[i] = 0;
          continue;
        }

        fields[i] = static_cast<uint64_t>(std::stoull(temp));
      }

      CacheMetrics &cm = lineMetrics[{currentFile, lineNumber}];
      cm.Dr += fields[0];
      cm.D1mr += fields[1];
      cm.DLmr += fields[2];
      cm.Dw += fields[3];
      cm.D1mw += fields[4];
      cm.DLmw += fields[5];
    }
    errs() << "Parsed " << lineMetrics.size()
           << " annotated lines from cachegrind\n";
    return !lineMetrics.empty();
  }

    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {

    if (CacheCGFile.empty()) {
      errs() << "No file provided via -cache-cg-file\n";
      return PreservedAnalyses::all();
    }

    if (!parseInputFile(CacheCGFile)) {
      errs() << "Failed to parse file: " << CacheCGFile << "\n";
      return PreservedAnalyses::all();
    }

    // Optional: keep this for debugging
    errs() << "===== Parsed Cachegrind Line Metrics =====\n";
    for (const auto &entry : lineMetrics) {
      const auto &file = entry.first.first;
      int line = entry.first.second;
      const CacheMetrics &cm = entry.second;

      errs() << file << ":" << line << "  Dr=" << cm.Dr << "  D1mr=" << cm.D1mr
             << "  DLmr=" << cm.DLmr << "  Dw=" << cm.Dw << "  D1mw=" << cm.D1mw
             << "  DLmw=" << cm.DLmw << "\n";
    }
    errs() << "===== End of Cachegrind Metrics =====\n";

    bool Changed = false;

    // Walk all functions/blocks/instructions
    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          // Only care about loads and stores
          if (!isa<LoadInst>(&I) && !isa<StoreInst>(&I))
            continue;

          // Need debug info to map back to source line
          const DebugLoc &DL = I.getDebugLoc();
          if (!DL)
            continue;

          auto *Scope = dyn_cast<DIScope>(DL.getScope());
          if (!Scope)
            continue;

          std::string FileName = Scope->getFilename().str();
          int Line = DL.getLine();

          FileLinePair fl(FileName, Line);

          if (!isHotLine(fl))
            continue;

          errs() << "Inserting prefetch for hot line " << FileName << ":"
                 << Line << "\n";

          if (insertPrefetch(&I))
            Changed = true;
        }
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

};

extern "C" PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ParseCachegrindPass", "v0.1",
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "parse-cachegrind") {
                    MPM.addPass(ParseCachegrindPass());
                    return true;
                  }
                  return false;
                });
          }};
}
