# eecs583project

This repo contains our EECS 583 term project:

- `profiler/` — LLVM pass plugin(s) for cache profiling / optimization.
- `benchmarks/` — C benchmarks we run our passes on.

This project implements a Cachegrind-guided LLVM optimization pass that inserts llvm.prefetch instructions for memory operations responsible for high cache-miss counts.

We built a full automated pipeline (run.sh) that:

1. Compiles the program normally (baseline).

2. Profiles it with Cachegrind and annotates miss counts.

3. Uses our LLVM pass to insert prefetches on “hot” memory-access lines.

4. Rebuilds the optimized program.

5. Times both versions and prints performance + cache statistics.

---

## Requirements
To reproduce our results, the experiments must be run on the EECS583A server:

```eecs583a.eecs.umich.edu```

Do NOT run on eecs583b — that machine has a newer, more aggressive CPU microarchitecture with built-in hardware prefetchers. On that machine, software prefetching provides little or inconsistent benefit, which masks the improvements made by our pass.

## Building the LLVM Pass
The pass is located under:
```./profiler/ParseCachegrindPass.cpp```

Before running, ensure the project is configured and built.

From your **project root**:

```bash
mkdir -p build
cd build
cmake -DLLVM_DIR=$(llvm-config --cmakedir) ..
cmake --build . -j
```

You’ll get:

`build/profiler/ParseCachegrindPass.so` — our LLVM pass plugin (cache-opt pass).

`build/benchmarks/` — source benchmarks live in ../benchmarks/*.c.

## Running the pass on a benchmark
From the root of the repository, run:
```./run.sh ./benchmarks/matmul_bad.c```


This executes the entire workflow:

1. Compiles baseline IR + binary
2. Times the baseline (multiple runs → average)
3. Runs Cachegrind
4. Annotates cache misses
5. Applies our LLVM pass guided by miss data
6. Recompiles the optimized binary
7. Times the optimized version
8. Runs Cachegrind again
9. Prints summary tables and % speedup/slowdown
