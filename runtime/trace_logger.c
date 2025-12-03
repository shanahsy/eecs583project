# runtime/CMakeLists.txt

add_library(cp_runtime STATIC
    trace_logger.c
)

target_include_directories(cp_runtime
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# You normally don't need LLVM here; it's plain C runtime support.
# But if you later add LLVM-dependent stuff, you can link it similarly.
