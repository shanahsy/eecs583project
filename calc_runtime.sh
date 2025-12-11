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

# Threshold values to sweep for the LLVM pass
THRESHOLDS=(100 500 1000 5000 10000 20000 30000 40000 50000)

# Number of runs for averaging
NUM_RUNS=10

show_help() {
  cat << EOF
Usage: $0 <source_file.c> [program args...]

Runs entire workflow:
  0. Compile LLVM Pass
  1. Compile to LLVM IR
  2. Build baseline binary
  2.5 Time baseline over ${NUM_RUNS} runs
  3. Run Cachegrind
  4. Run cg_annotate
  5. Apply CacheOpt LLVM pass (default threshold)
  6. Build optimized binary
  6.5 Time optimized (default threshold) over ${NUM_RUNS} runs
  7. Run Cachegrind optimized
  8. Run cg_annotate optimized
  9. Sweep thresholds, time each over ${NUM_RUNS} runs, plot runtime vs threshold

Options:
  -k      Keep intermediate files (no cleanup)
  -h      Show help

Example:
  $0 ./benchmarks/bad_matrix_walk.c
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
    echo "Error: Missing <source_file.c>"
    show_help
    exit 1
fi

SRC_FILE=$1
shift
PROG_ARGS=("$@")

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

# Data + plot for threshold sweep
THRESH_DATA="$NAME.threshold_times.dat"
THRESH_PLOT="$NAME.threshold_vs_time.png"

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
# STEP 2.5: Time baseline (real wall-clock)
###############################################
echo "[2.5] Timing baseline (wall-clock, ${NUM_RUNS} runs)…"
BASE_AVG=$(measure_avg_time "$NUM_RUNS" "$BIN_ORIG" "${PROG_ARGS[@]}")
echo "BASELINE average over ${NUM_RUNS} runs: ${BASE_AVG} s"

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
# STEP 5: Apply the LLVM optimization pass (default threshold)
###############################################
echo "[5] Running CacheOpt LLVM pass (default threshold)…"
opt \
  -load-pass-plugin "$PASS" \
  -passes="parse-cachegrind" \
  -cache-cg-file="$CG_ANN" \
  "$IR_ORIG" -o "$IR_OPT"

###############################################
# STEP 6: Build new binary (default threshold)
###############################################
echo "[6] Building new binary…"
clang -O0 -g "$IR_OPT" -o "$BIN_OPT"

###############################################
# STEP 6.5: Time optimized (default threshold)
###############################################
echo "[6.5] Timing optimized (default threshold, ${NUM_RUNS} runs)…"
OPT_AVG=$(measure_avg_time "$NUM_RUNS" "$BIN_OPT" "${PROG_ARGS[@]}")
echo "OPTIMIZED (default threshold) average over ${NUM_RUNS} runs: ${OPT_AVG} s"

###############################################
# STEP 7: Cachegrind optimized
###############################################
echo "[7] Running Cachegrind optimized version…"
valgrind --tool=cachegrind \
  --cache-sim=yes --branch-sim=no \
  --cachegrind-out-file="$CG_RAW_OPT" \
  "$BIN_OPT" "${PROG_ARGS[@]}"

###############################################
# STEP 8: Annotate optimized output
###############################################
echo "[8] Running cg_annotate on optimized output…"
cg_annotate --auto=yes --show-percs=no "$CG_RAW_OPT" > "$CG_ANN_OPT"

###############################################
# STEP 9: Sweep thresholds & measure runtime
###############################################
echo "[9] Sweeping thresholds and measuring average runtime…"
echo "# threshold  avg_runtime_seconds" > "$THRESH_DATA"

for TH in "${THRESHOLDS[@]}"; do
  echo "  -> threshold=${TH}"
  IR_OPT_TH="${NAME}.t${TH}.opt.ll"
  BIN_OPT_TH="${NAME}.t${TH}.opt"

  # Re-run opt with a different miss threshold
  opt \
    -load-pass-plugin "$PASS" \
    -passes="parse-cachegrind" \
    -cache-cg-file="$CG_ANN" \
    -cache-miss-threshold="$TH" \
    "$IR_ORIG" -o "$IR_OPT_TH"

  # Build binary for this threshold
  clang -O0 -g "$IR_OPT_TH" -o "$BIN_OPT_TH"

  # Time it NUM_RUNS times and average
  AVG_TH=$(measure_avg_time "$NUM_RUNS" "$BIN_OPT_TH" "${PROG_ARGS[@]}")
  echo "     avg runtime: ${AVG_TH} s"
  echo "$TH $AVG_TH" >> "$THRESH_DATA"
done

###############################################
# STEP 10: Plot time vs threshold
###############################################
echo "[10] Generating plot of avg runtime vs threshold…"
python3 - << EOF
import matplotlib.pyplot as plt

xs = []
ys = []
with open("$THRESH_DATA", "r") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        th_str, t_str = line.split()
        xs.append(float(th_str))
        ys.append(float(t_str))

plt.figure()
plt.plot(xs, ys, marker="o")
plt.xlabel("cache-miss-threshold")
plt.ylabel("Average runtime (s)")
plt.title("Runtime vs Miss Threshold for $BASENAME")
plt.grid(True)
plt.tight_layout()
plt.savefig("$THRESH_PLOT")
EOF

echo "  -> Wrote data to: $THRESH_DATA"
echo "  -> Wrote plot to: $THRESH_PLOT"

###############################################
# CLEANUP
###############################################
if $CLEAN; then
  rm -f *.cgann *.bc *.profdata *.ll *.orig *.opt
fi

echo -e "\nDone ✔"
