#!/usr/bin/env bash
# Automated pipeline for Cachegrind-guided optimization
# Usage:
#   ./run_cacheopt.sh <sourcefile.c>

set -e

###############################################
# CONFIG
###############################################

PASS="./build/profiler/ParseCachegrindPass.so"
CLEAN=true     # set to false if you want to keep IR/output files

show_help() {
  cat << EOF
Usage: $0 <source_file.c>

Runs entire workflow:
  0. Compile LLVM Pass
  1. Compile to LLVM IR
  2. Build baseline binary
  3. Run Cachegrind
  4. Run cg_annotate
  5. Apply CacheOpt LLVM pass
  6. Build optimized binary
  7. Run Cachegrind again
  8. Compare results

Options:
  -k      Keep intermediate files (no cleanup)
  -h      Show help

Example:
  $0 ./benchmarks/bad_matrix_walk.c
EOF
}

###############################################
# PARSE FLAGS
###############################################
while getopts ":kh" opt; do
    case $opt in
        k) CLEAN=false ;;
        h) show_help; exit 0 ;;
        \?)
            echo "Invalid option: -$OPTARG" >&2
            show_help
            exit 1
            ;;
    esac
done

shift $((OPTIND -1))

###############################################
# CHECK ARGUMENT
###############################################
if [ $# -lt 1 ]; then
    echo "Error: Missing <source_file.c>"
    show_help
    exit 1
fi

SRC_FILE=$1

if [ ! -f "$SRC_FILE" ]; then
    echo "Error: $SRC_FILE does not exist."
    exit 1
fi

if [ ! -f "$PASS" ]; then
    echo "ERROR: Could not find LLVM pass at: $PASS"
    exit 1
fi

###############################################
# DERIVED NAMES
###############################################
BASENAME=$(basename "$SRC_FILE")
NAME="./build/${BASENAME%.*}"

IR_ORIG="$NAME.ll"
IR_OPT="$NAME.opt.ll"
BIN_ORIG="$NAME.orig"
BIN_OPT="$NAME.opt"
CG_RAW="$NAME.cg"
CG_ANN="$NAME.cgann"
CG_RAW_OPT="$NAME.opt.cg"
CG_ANN_OPT="$NAME.opt.cgann"

###############################################
# CLEAN OLD FILES
###############################################
rm -f *.ll *.cg *.cgann *.opt.* *.orig *.opt

###############################################
# STEP 0: Compile LLVM Pass
###############################################
echo "[0] Compiling LLVM Pass…"
cmake --build ./build/ -j

###############################################
# STEP 1: Compile original program to LLVM IR
###############################################
echo "[1] Compiling to LLVM IR…"
clang -O0 -g -emit-llvm -S "$SRC_FILE" -o "$IR_ORIG"

###############################################
# STEP 2: Build baseline binary
###############################################
echo "[2] Building baseline binary…"
clang -O0 -g "$SRC_FILE" -o "$BIN_ORIG"

###############################################
# STEP 3: Run baseline Cachegrind
###############################################
echo "[3] Running Cachegrind baseline…"
valgrind --tool=cachegrind \
  --cache-sim=yes --branch-sim=no \
  --cachegrind-out-file="$CG_RAW" \
  "$BIN_ORIG" 

###############################################
# STEP 4: Annotate Cachegrind output
###############################################
echo "[4] Running cg_annotate…"
cg_annotate "$CG_RAW" > "$CG_ANN"

###############################################
# STEP 5: Apply the LLVM optimization pass
###############################################
echo "[5] Running CacheOpt LLVM pass…"
opt \
  -load-pass-plugin "$PASS" \
  -passes="parse-cachegrind" \
  -cache-cg-file="$CG_ANN" \
  "$IR_ORIG" -o "$IR_OPT"

###############################################
# STEP 6: Build optimized binary
###############################################
echo "[6] Building optimized binary…"
clang -O2 "$IR_OPT" -o "$BIN_OPT"

###############################################
# STEP 7: Cachegrind optimized
###############################################
echo "[7] Running Cachegrind optimized version…"
valgrind --tool=cachegrind \
  --cache-sim=yes --branch-sim=no \
  --cachegrind-out-file="$CG_RAW_OPT" \
  "./$BIN_OPT"

###############################################
# STEP 8: Annotate optimized output
###############################################
echo "[8] Running cg_annotate on optimized output…"
cg_annotate "$CG_RAW_OPT" > "$CG_ANN_OPT"

###############################################
# SUMMARY
###############################################

echo -e "\n================ BASELINE =================="
grep "PROGRAM TOTALS" -A1 "$CG_ANN"

echo -e "\n================ OPTIMIZED =================="
grep "PROGRAM TOTALS" -A1 "$CG_ANN_OPT"

###############################################
# CLEANUP
###############################################
if $CLEAN; then
  rm -f *.cgann *.bc *.profdata *.ll *.orig *.opt
fi

echo -e "\nDone ✔"
