#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

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
      errs() << line << "\n";

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

    return PreservedAnalyses::all();
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
