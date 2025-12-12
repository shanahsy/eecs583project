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
#include "llvm/ADT/SmallVector.h"

#include <algorithm> // for std::remove
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>

using namespace llvm;

typedef std::pair<std::string, int> FileLinePair;

static cl::opt<std::string> CacheCGFile(
    "cache-cg-file",
    cl::desc("Path to cg_annotate output"),
    cl::init(""));

static cl::opt<uint64_t> MissThreshold(
    "cache-miss-threshold",
    cl::desc("Total data cache misses (D1mr+DLmr+D1mw+DLmw) needed to prefetch"),
    // cl::init(3000000000)); // tweak via CLI
    cl::init(100)); 

static cl::opt<unsigned> PrefetchDistance(
    "cache-prefetch-distance",
    cl::desc("Prefetch this many elements ahead along a non-constant GEP index"),
    cl::init(4)); // default lookahead

// Constants
const std::string AutoAnnotatedPrefix = "-- Auto-annotated source:";
const std::string EndAnnotatedBlock =
    "--------------------------------------------------------------------------"
    "------";

static std::string normalizeFileName(const std::string &Path) {
  std::string S = Path;
  // Strip directory components: keep only the basename
  auto pos = S.find_last_of("/\\");
  if (pos != std::string::npos)
    S = S.substr(pos + 1);
  return S;
}

/**
 * Metrics from Cachegrind for a specific line.
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

  /// Maps (filename, line number) -> cachegrind information
  std::map<FileLinePair, CacheMetrics> lineMetrics;

  bool isHotLine(const FileLinePair &fl) {
    auto it = lineMetrics.find(fl);
    if (it == lineMetrics.end())
      return false;

    const CacheMetrics &cm = it->second;
    uint64_t totalMisses = cm.D1mr + cm.DLmr + cm.D1mw + cm.DLmw;
    return totalMisses >= MissThreshold;
  }

  /// Try to compute a "future" address for prefetching by bumping a
  /// non-constant index of a GEP by PrefetchDistance. Returns nullptr
  /// if we can't do anything better than the original address.
  Value *computeFutureAddress(Value *Addr, IRBuilder<> &Builder) {
    // Only handle GetElementPtr for now. This covers typical array/pointer code.
    auto *GEP = dyn_cast<GetElementPtrInst>(Addr);
    if (!GEP)
      return nullptr;

    SmallVector<Value *, 8> NewIndices;
    bool Updated = false;

    // Copy all indices, but bump the first non-constant integer index we see.
    for (Value *Idx : GEP->indices()) {
      if (!Updated &&
          !isa<Constant>(Idx) &&
          Idx->getType()->isIntegerTy()) {
        // idx' = idx + PrefetchDistance
        Value *Off = Builder.getIntN(
            Idx->getType()->getIntegerBitWidth(), PrefetchDistance);
        Value *NewIdx = Builder.CreateAdd(Idx, Off, "prefetch.idx");
        NewIndices.push_back(NewIdx);
        Updated = true;
      } else {
        NewIndices.push_back(Idx);
      }
    }

    if (!Updated)
      return nullptr; // nothing to bump -> can't compute a future address

    // Build a new GEP with the updated indices
    return Builder.CreateGEP(
        GEP->getSourceElementType(),
        GEP->getPointerOperand(),
        NewIndices,
        "prefetch.addr");
  }

  /// Insert a prefetch call, ideally on a future address derived from the
  /// current load/store address. Falls back to prefetching the same address
  /// if we can't analyze the pattern.
  bool insertPrefetch(Instruction *I) {
  IRBuilder<> Builder(I);

  Value *Addr = nullptr;

  if (auto *LI = dyn_cast<LoadInst>(I)) {
    // Only prefetch loads
    Addr = LI->getPointerOperand();
  } else if (auto *SI = dyn_cast<StoreInst>(I)) {
    // Skip stores entirely for now
    return false;
  } else {
    return false; // not a load/store
  }

  if (!Addr)
    return false;

  Module *M = I->getModule();
  LLVMContext &Ctx = M->getContext();

  // Try to compute a "future" address in the same loop
  Value *PrefAddr = computeFutureAddress(Addr, Builder);
  if (!PrefAddr) {
    // Fall back to prefetching the same address (still sometimes useful)
    PrefAddr = Addr;
  }

  // Cast to i8* as required by llvm.prefetch
  Type *I8PtrTy = Type::getInt8Ty(Ctx)->getPointerTo();
  Value *AddrI8 = Builder.CreateBitCast(PrefAddr, I8PtrTy);

  // declare void @llvm.prefetch.p0(i8* addr, i32 rw, i32 locality, i32 cache_type)
  Function *PrefetchFn =
      Intrinsic::getDeclaration(M, Intrinsic::prefetch, {I8PtrTy});

  // rw: 0 = read, 1 = write
  // locality: 0 (none) .. 3 (high)
  // cache_type: 1 = data cache
  Value *RW        = Builder.getInt32(0); // read prefetch
  Value *Locality  = Builder.getInt32(3); // high locality
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
      // Start of an annotated block: "-- Auto-annotated source:  filename"
      if (line.rfind(AutoAnnotatedPrefix, 0) == 0) {
        auto filename = line.substr(AutoAnnotatedPrefix.size());
        std::string trimmed =
            std::regex_replace(filename, std::regex("^ +| +$|( ) +"), "$1");

        currentFile = normalizeFileName(trimmed);
        lineNumber = 0;
        std::getline(ifs, line); // skip first dashed block
        continue;
      }

      // End of an annotated block (but skip the first dashed line after filename)
      if (!currentFile.empty() &&
          line.rfind(EndAnnotatedBlock, 0) == 0 &&
          lineNumber != 0) {
        currentFile.clear();
        continue;
      }

      // Not in an annotated block? Skip.
      if (currentFile.empty())
        continue;

      // Skip header row: "Ir I1mr ILmr ..."
      if (line.rfind("Ir", 0) == 0)
        continue;

      // Blank line: don't advance lineNumber
      if (line.empty())
        continue;

      // Left trim spaces
      auto trimmedLine = std::regex_replace(line, std::regex("^ +"), "$1");

      // Handle line skips: "-- line 39 -----------"
      if (trimmedLine.rfind("-- line", 0) == 0) {
        std::stringstream ss(trimmedLine.substr(std::strlen("-- line")));
        int ln = 0;
        ss >> ln;
        lineNumber = ln - 1;
        continue;
      }

      ++lineNumber;

      // Sanity check: first token should start with a digit
      if (trimmedLine.empty() ||
          !std::isdigit(static_cast<unsigned char>(trimmedLine[0]))) {
        continue;
      }

      std::stringstream ss(trimmedLine);
      uint64_t fields[6];
      std::string temp;

      // Skip first three fields (Ir I1mr ILmr)
      ss >> temp >> temp >> temp;

      // Read Dr, D1mr, DLmr, Dw, D1mw, DLmw
      for (int i = 0; i < 6; ++i) {
        if (!(ss >> temp)) {
          fields[i] = 0;
          continue;
        }

        // Strip commas from numbers like "7,004"
        temp.erase(std::remove(temp.begin(), temp.end(), ','), temp.end());

        if (temp.empty() ||
            !std::isdigit(static_cast<unsigned char>(temp[0]))) {
          fields[i] = 0;
          continue;
        }

        fields[i] = static_cast<uint64_t>(std::stoull(temp));
      }

      CacheMetrics &cm = lineMetrics[{currentFile, lineNumber}];
      cm.Dr   += fields[0];
      cm.D1mr += fields[1];
      cm.DLmr += fields[2];
      cm.Dw   += fields[3];
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

    // Optional: debug dump of parsed metrics
    errs() << "===== Parsed Cachegrind Line Metrics =====\n";
    for (const auto &entry : lineMetrics) {
      const auto &file = entry.first.first;
      int line = entry.first.second;
      const CacheMetrics &cm = entry.second;

      errs() << file << ":" << line
             << "  Dr="   << cm.Dr
             << "  D1mr=" << cm.D1mr
             << "  DLmr=" << cm.DLmr
             << "  Dw="   << cm.Dw
             << "  D1mw=" << cm.D1mw
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

          std::string FileName = normalizeFileName(Scope->getFilename().str());
          int Line = DL.getLine();

          FileLinePair fl(FileName, Line);

          if (!isHotLine(fl))
            continue;

          errs() << "Inserting prefetch for hot line "
                 << FileName << ":" << Line << "\n";

          if (insertPrefetch(&I))
            Changed = true;
        }
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

extern "C" PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION,
      "ParseCachegrindPass",
      "v0.2", // bumped version
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name,
               ModulePassManager &MPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "parse-cachegrind") {
                MPM.addPass(ParseCachegrindPass());
                return true;
              }
              return false;
            });
      }};
}
