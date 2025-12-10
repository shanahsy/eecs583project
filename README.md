# eecs583project

This repo contains our EECS 583 term project:

- `profiler/` — LLVM pass plugin(s) for cache profiling / optimization.
- `runtime/` — support library (e.g., logging runtime, if needed later).
- `benchmarks/` — C benchmarks we run our passes on.

---

## Building

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

## Running the pass on a benchmark

Example: run `CacheOptPass` on `bad_matrix_walk.c` to dump all load/store → (file, line) mappings.

### 1. Compile the benchmark to LLVM IR (unoptimized, with debug)

From `build/`:
```bash
clang -O0 -g \
  ../benchmarks/bad_matrix_walk.c \
  -o bad_matrix_walk.out
clang -O0 -g -emit-llvm -S \
  ../benchmarks/bad_matrix_walk.c \
  -o bad_matrix_walk.ll
```
- -O0 so LLVM doesn’t optimize away the loops/loads.
- -g so debug info exists (we can map to source file + line).

### 2. Running Cachegrind
From `build/`:
```bash
valgrind --tool=cachegrind ./bad_matrix_walk.out -o cachegrind.out
cg_annotate --auto=yes --show-percs=no ./cachegrind.out > cg-annotate.out
```

### 3. Run the LLVM pass plugin

From `build/`:
```bash
opt \
  -load-pass-plugin ./profiler/ParseCachegrindPass.so \
  -passes="parse-cachegrind" \
  -cache-cg-file=cg-annotate.out
  bad_matrix_walk.ll \
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

for every load / store that has debug info.

