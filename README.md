# eecs583project

From your project root:

mkdir build
cd build
cmake ..
cmake --build . -j


You’ll get:

profiler/CacheProfilerPass.{so,dylib} — your LLVM pass plugin.

runtime/libcp_runtime.a — your logging runtime.

benchmarks/matmul, benchmarks/linked_list, etc.

Then a typical workflow:

# Instrument a benchmark
clang -O0 -emit-llvm -c ../benchmarks/matmul.c -o matmul.bc

opt -load-pass-plugin=./profiler/CacheProfilerPass.dylib \
    -passes="function(cache-profiler)" \
    matmul.bc -o matmul.prof.bc

# Compile with runtime
clang matmul.prof.bc runtime/libcp_runtime.a -o matmul.prof

# Run with trace output
CP_TRACE_FILE=matmul.trace ./matmul.prof


From there, your tools/convert_to_dinero.py + run_cache_sim.sh can eat matmul.trace.
