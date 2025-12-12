# benchmarks/CMakeLists.txt

# List of benchmarks to build.
# Add more .c files here as you create them.
set(BENCHMARK_SOURCES
    # matmul.c
    # linked_list.c
    # add more like: jacobi2d.c, tree_traversal.c, etc.
)

foreach(src ${BENCHMARK_SOURCES})
    # Get the base name (without extension) for the executable name
    get_filename_component(bench_name ${src} NAME_WE)

    add_executable(${bench_name}
        ${src}
    )

    # Link the runtime logger so the instrumentation calls resolve
    target_link_libraries(${bench_name}
        PRIVATE
        cp_runtime
    )
endforeach()