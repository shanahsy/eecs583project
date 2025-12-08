# eecs583project

This repo contains our EECS 583 term project:

- `profiler/` — LLVM pass plugin(s) for cache profiling / optimization.
- `runtime/` — support library (e.g., logging runtime, if needed later).
- `benchmarks/` — C benchmarks we run our passes on.

---

## Building
*Make sure to build this on the remote server `eecs583a.eecs.umich.edu`, unless you remember installing LLVM locally at the beginning of the CSE 583 semester.*

From your **project root**:

```bash
mkdir -p build
cd build
cmake -DLLVM_DIR=$(llvm-config --cmakedir) ..
cmake --build . -j
```

You’ll get:

`build/profiler/CacheOptPass.so` — our LLVM pass plugin (cache-opt pass).

`build/runtime/libcp_runtime.a` — runtime library (for future instrumentation / logging).

`build/benchmarks/` — source benchmarks live in ../benchmarks/*.c.

## Overview: What the CacheOptPass Does
CacheOptPass performs two key tasks:

1. **Static mapping generation**
For each load/store with debug info, it prints: 
`<InstID>,<Function>,<load/store>,<File>,<Line>` 
to stdout. This becomes static_map.csv.

2. **IR instrumentation**
It inserts a call: `call void @cacheopt_log(i32 ID, i8* addr, i32 isLoad)` before each load/store, enabling runtime logging of memory addresses.

At runtime, cacheopt_log produces a trace of:

`InstID,R/W,address`


This can be fed into any cache simulator.

## Running the pass on a benchmark

Example: run `CacheOptPass` on `bad_matrix_walk.c` to dump all load/store → (file, line) mappings.

Below is the complete workflow to generate:

- `static_map.csv` → instruction ↦ source mapping

- `bad_matrix_walk.instrumented.ll` → IR instrumented with logging

- `trace.csv` → runtime memory access trace

### 1. Compile the benchmark to LLVM IR (unoptimized, with debug)

From `build/`:
```bash
clang -O0 -g -emit-llvm -S \
  ../benchmarks/bad_matrix_walk.c \
  -o bad_matrix_walk.ll
```
- -O0 so LLVM doesn’t optimize away the loops/loads/memory operations
- -g so debug info exists (we can map to source file + line).
- -emit-llvm -S outputs textual .ll

### 2. Run CacheOptPass to instrument the IR
```bash
opt \
  -load-pass-plugin ./profiler/CacheOptPass.so \
  -passes="cache-opt" \
  bad_matrix_walk.ll \
  -o bad_matrix_walk.instrumented.ll \
  > static_map.csv
```

This produces:
1. `static_map.csv` which contains rows like:
```
0,main,store,../benchmarks/bad_matrix_walk.c,8
1,main,store,../benchmarks/bad_matrix_walk.c,10
2,main,load,../benchmarks/bad_matrix_walk.c,10
...
```

2. `bad_matrix_walk.instrumented.ll`
This IR file includes inserted calls to: `@cacheopt_log` for every identified load/store.

### 3. Build the instrumented executable
Compile the instrumented IR along with the logging function:
```bash
clang -O0 -g \
  bad_matrix_walk.instrumented.ll \
  ../cacheopt_log.c \
  -o bad_matrix_walk_instr
```
This executable will now **log all memory accesses at runtime**. 

### 4. Run the instrumented program and capture the trace
```bash
./bad_matrix_walk_instr 2> trace.csv
```
*This may take a couple seconds/minutes*

Why `2v`? 
Because `cacheopt_log.c` writes to **stderr* for clean separation.

Inspect output:
```bash
head trace.csv
```

Example:
```bash
0,W,0x7ffc57d79fa0
1,W,0x7ffc57d79f9c
2,R,0x7ffc57d79f9c
3,W,0x7ffc57d79f98
4,R,0x7ffc57d79f98
5,R,0x7ffc57d79f98
6,R,0x7ffc57d79f9c
7,R,0x55f284c81050
8,R,0x7ffc57d79fa0
9,W,0x7ffc57d79fa0
...
```
Meaning:
- Column 1 → Instruction ID (matches static_map.csv)
- Column 2 → R/W (load/store)
- Column 3 → Actual runtime memory address

This should hopefully be the input format needed for cache simulators. Voila, now we have a trace file!

## Summary Cheat Sheet (for `bad_matrix_walk.c`)

```bash
# 1) Generate LLVM IR
clang -O0 -g -emit-llvm -S \
    ../benchmarks/bad_matrix_walk.c \
    -o bad_matrix_walk.ll

# 2) Run LLVM pass → instrument + dump static map
opt -load-pass-plugin ./profiler/CacheOptPass.so \
    -passes="cache-opt" \
    bad_matrix_walk.ll \
    -o bad_matrix_walk.instrumented.ll \
    > static_map.csv

# 3) Build instrumented binary
clang -O0 -g \
    bad_matrix_walk.instrumented.ll \
    ../cacheopt_log.c \
    -o bad_matrix_walk_instr

# 4) Run program → collect trace
./bad_matrix_walk_instr 2> trace.csv

# Optional: inspect
head trace.csv

```

<!-- Old instructions, leaving here in case we still need it? -->
<!-- ### 2. Run the LLVM pass plugin

From `build/`:
```bash
opt \
  -load-pass-plugin ./profiler/CacheOptPass.so \
  -passes="cache-opt" \
  bad_matrix_walk.ll \
  -disable-output > load_store_map.csv
```

This:
- Loads our plugin CacheOptPass.so.
- Runs the cache-opt module pass over bad_matrix_walk.ll.
- Does not emit modified IR (-disable-output), just runs the pass.
- Redirects everything the pass prints (e.g.,
```
main,load,../benchmarks/bad_matrix_walk.c,12
main,store,../benchmarks/bad_matrix_walk.c,8
...
```
)
into `load_store_map.csv`.


`load_store_map.csv` is effectively:

`<function>,<kind>,<filename>,<line>`

for every load / store that has debug info. -->

