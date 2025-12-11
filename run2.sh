#!/usr/bin/env bash
# Automated pipeline for Cachegrind-guided optimization
# Usage:
#   ./run.sh <sourcefile.c>
#   ./run.sh <directory_with_c_files>

set -e

###############################################
# CONFIG
###############################################

PASS="./build/profiler/ParseCachegrindPass.so"
CLEAN=true     # set to false if you want to keep IR/output files
NUM_RUNS=5     # runs per benchmark for timing

show_help() {
  cat << EOF
Usage: $0 <source_file.c | directory> [program args...]

Runs entire workflow:
  0. Compile LLVM Pass
  1. Compile to LLVM IR
  2. Build baseline binary
  2.5 Time baseline over ${NUM_RUNS} runs
  3. Run Cachegrind
  4. Run cg_annotate
  5. Apply CacheOpt LLVM pass
  6. Build optimized binary
  6.5 Time optimized over ${NUM_RUNS} runs
  7. Run Cachegrind again
  8. Print cache stats

If <source_file.c> is a directory, runs the pipeline for every *.c
file in that directory and prints the average % runtime change,
plus the best (most negative) and worst (most positive) % change.

Options:
  -k      Keep intermediate files (no cleanup)
  -h      Show help

Example:
  $0 ./benchmarks/chat_benchmarks/matmul_bad.c
  $0 ./benchmarks/chat_benchmarks
EOF
}

###############################################
# HELPER: measure average runtime over N runs
###############################################
measure_avg_time() {
  local runs="$1"
  shift
  local bin="$1"
  shift

  local sum="0.0"
  for ((i=1; i<=runs; ++i)); do
    # %e = real time in seconds (float)
    local t
    t=$(/usr/bin/time -f "%e" "$bin" "$@" 2>&1 >/dev/null)
    sum=$(echo "$sum + $t" | bc -l)
  done

  echo "scale=6; $sum / $runs" | bc -l
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
    echo "Error: Missing <source_file.c | directory>"
    show_help
    exit 1
fi

TARGET=$1
shift
PROG_ARGS=("$@")

if [ ! -f "$PASS" ]; then
    echo "ERROR: Could not find LLVM pass at: $PASS"
    exit 1
fi

###############################################
# STEP 0: Compile LLVM Pass ONCE
###############################################
echo "[0] Compiling LLVM Pass…"
cmake --build ./build/ -j

###############################################
# PER-BENCHMARK PIPELINE
###############################################
run_one_benchmark() {
  local SRC_FILE="$1"

  if [ ! -f "$SRC_FILE" ]; then
      echo "Error: $SRC_FILE does not exist."
      return 1
  fi

  echo
  echo "==================== Benchmark: $SRC_FILE ===================="

  local BASENAME NAME
  local IR_ORIG IR_OPT BIN_ORIG BIN_OPT
  local CG_RAW CG_ANN CG_RAW_OPT CG_ANN_OPT

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

  # Clean old files for this benchmark (best-effort)
  rm -f "$IR_ORIG" "$IR_OPT" "$BIN_ORIG" "$BIN_OPT" \
        "$CG_RAW" "$CG_ANN" "$CG_RAW_OPT" "$CG_ANN_OPT"

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
  # STEP 2.5: Time baseline (real wall-clock)
  ###############################################
  echo "[2.5] Timing baseline (wall-clock, ${NUM_RUNS} runs)…"
  local BASE_AVG
  BASE_AVG=$(measure_avg_time "$NUM_RUNS" "$BIN_ORIG" "${PROG_ARGS[@]}")
  echo "  BASELINE average: ${BASE_AVG} s"

  ###############################################
  # STEP 3: Run baseline Cachegrind
  ###############################################
  echo "[3] Running Cachegrind baseline…"
  valgrind --tool=cachegrind \
    --cache-sim=yes --branch-sim=no \
    --cachegrind-out-file="$CG_RAW" \
    "$BIN_ORIG" "${PROG_ARGS[@]}"

  ###############################################
  # STEP 4: Annotate Cachegrind output
  ###############################################
  echo "[4] Running cg_annotate…"
  cg_annotate --auto=yes --show-percs=no "$CG_RAW" > "$CG_ANN"

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
  # STEP 6: Build new binary
  ###############################################
  echo "[6] Building new binary…"
  clang -O0 -g "$IR_OPT" -o "$BIN_OPT"

  ###############################################
  # STEP 6.5: Time optimized (real wall-clock)
  ###############################################
  echo "[6.5] Timing optimized (wall-clock, ${NUM_RUNS} runs)…"
  local OPT_AVG
  OPT_AVG=$(measure_avg_time "$NUM_RUNS" "$BIN_OPT" "${PROG_ARGS[@]}")
  echo "  OPTIMIZED average: ${OPT_AVG} s"

  ###############################################
  # STEP 7: Cachegrind optimized
  ###############################################
  echo "[7] Running Cachegrind optimized version…"
  valgrind --tool=cachegrind \
    --cache-sim=yes --branch-sim=no \
    --cachegrind-out-file="$CG_RAW_OPT" \
    "./$BIN_OPT" "${PROG_ARGS[@]}"

  ###############################################
  # STEP 8: Annotate optimized output
  ###############################################
  echo "[8] Running cg_annotate on optimized output…"
  cg_annotate --auto=yes --show-percs=no "$CG_RAW_OPT" > "$CG_ANN_OPT"

  ###############################################
  # SUMMARY FOR THIS BENCHMARK
  ###############################################
  echo "[Summary] Cache stats for $SRC_FILE"

  print_program_totals_line() {
      local line="$1"
      read -r ir i1mr ilmr dr d1mr dlmr dw d1mw dlmw _ <<< "$line"
      printf "%-12s %-8s %-8s %-12s %-8s %-8s %-12s %-8s %-8s\n" \
          "Ir" "I1mr" "ILmr" "Dr" "D1mr" "DLmr" "Dw" "D1mw" "DLmw"
      printf "%-12s %-8s %-8s %-12s %-8s %-8s %-12s %-8s %-8s\n" \
          "$ir" "$i1mr" "$ilmr" "$dr" "$d1mr" "$dlmr" "$dw" "$d1mw" "$dlmw"
  }

  echo -e "\n  --- BASELINE ---"
  local totals_line
  totals_line=$(grep "PROGRAM TOTALS" -A1 "$CG_ANN" | tail -n1)
  print_program_totals_line "$totals_line"

  echo -e "\n  --- OPTIMIZED ---"
  totals_line=$(grep "PROGRAM TOTALS" -A1 "$CG_ANN_OPT" | tail -n1)
  print_program_totals_line "$totals_line"

  # Percentage change in runtime
  local PERC
  PERC=$(echo "100.0 * ($OPT_AVG - $BASE_AVG) / $BASE_AVG" | bc -l)
  echo
  echo "  Runtime change for $SRC_FILE: ${PERC}%  (positive = slower, negative = speedup)"

  # Export per-benchmark numbers back to caller via globals
  LAST_BASE_AVG="$BASE_AVG"
  LAST_OPT_AVG="$OPT_AVG"
  LAST_PERC="$PERC"
  LAST_FILE="$SRC_FILE"

  # Cleanup per-benchmark intermediates if requested
  if $CLEAN; then
    rm -f "$IR_ORIG" "$IR_OPT" "$BIN_ORIG" "$BIN_OPT" \
          "$CG_RAW" "$CG_ANN" "$CG_RAW_OPT" "$CG_ANN_OPT"
  fi
}

###############################################
# MAIN: single file vs directory mode
###############################################
TOTAL_PERC="0.0"
BENCH_COUNT=0

BEST_FILE=""
BEST_BASE=""
BEST_OPT=""
BEST_PERC=""

WORST_FILE=""
WORST_BASE=""
WORST_OPT=""
WORST_PERC=""

if [ -d "$TARGET" ]; then
  # Directory mode: run all *.c files
  DIR="$TARGET"
  echo "Running all *.c benchmarks in directory: $DIR"

  shopt -s nullglob
  files=("$DIR"/*.c)
  shopt -u nullglob

  if [ ${#files[@]} -eq 0 ]; then
    echo "No .c files found in $DIR"
    exit 1
  fi

  for f in "${files[@]}"; do
    run_one_benchmark "$f"
    TOTAL_PERC=$(echo "$TOTAL_PERC + $LAST_PERC" | bc -l)
    BENCH_COUNT=$((BENCH_COUNT + 1))

    # Initialize best/worst on the first benchmark
    if [ -z "$BEST_FILE" ]; then
      BEST_FILE="$LAST_FILE"
      BEST_BASE="$LAST_BASE_AVG"
      BEST_OPT="$LAST_OPT_AVG"
      BEST_PERC="$LAST_PERC"

      WORST_FILE="$LAST_FILE"
      WORST_BASE="$LAST_BASE_AVG"
      WORST_OPT="$LAST_OPT_AVG"
      WORST_PERC="$LAST_PERC"
    else
      # Compare for best (most negative) speedup
      if [ "$(echo "$LAST_PERC < $BEST_PERC" | bc -l)" -eq 1 ]; then
        BEST_FILE="$LAST_FILE"
        BEST_BASE="$LAST_BASE_AVG"
        BEST_OPT="$LAST_OPT_AVG"
        BEST_PERC="$LAST_PERC"
      fi

      # Compare for worst (largest positive slowdown)
      if [ "$(echo "$LAST_PERC > $WORST_PERC" | bc -l)" -eq 1 ]; then
        WORST_FILE="$LAST_FILE"
        WORST_BASE="$LAST_BASE_AVG"
        WORST_OPT="$LAST_OPT_AVG"
        WORST_PERC="$LAST_PERC"
      fi
    fi
  done

  if [ "$BENCH_COUNT" -gt 0 ]; then
    AVG_PERC=$(echo "scale=6; $TOTAL_PERC / $BENCH_COUNT" | bc -l)
    echo
    echo "====================================================="
    echo "Average % runtime change over $BENCH_COUNT benchmarks:"
    echo "  ${AVG_PERC}%  (positive = slowdown, negative = speedup)"
    echo "====================================================="

    echo
    echo "Best file (most negative % = biggest speedup):"
    echo "  File:      $BEST_FILE"
    echo "  Base avg:  ${BEST_BASE} s"
    echo "  Opt avg:   ${BEST_OPT} s"
    echo "  % change:  ${BEST_PERC}%"

    echo
    echo "Worst file (most positive % = biggest slowdown):"
    echo "  File:      $WORST_FILE"
    echo "  Base avg:  ${WORST_BASE} s"
    echo "  Opt avg:   ${WORST_OPT} s"
    echo "  % change:  ${WORST_PERC}%"
    echo "====================================================="
  fi

else
  # Single-file mode
  run_one_benchmark "$TARGET"
  echo
  echo "Single benchmark % runtime change: ${LAST_PERC}%"
fi

echo -e "\nDone ✔"
