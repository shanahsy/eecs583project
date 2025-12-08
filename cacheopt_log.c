// cacheopt_log.c
#include <stdio.h>
#include <stdint.h>

// Called from LLVM-instrumented code:
//   id     : static ID of the instruction (same as in your compile-time map)
//   addr   : runtime address being accessed
//   isLoad : 1 for load, 0 for store
void cacheopt_log(int id, void *addr, int isLoad) {
    // Format: id,op,address
    // op: 'R' for load, 'W' for store
    fprintf(stderr, "%d,%c,%p\n",
            id,
            isLoad ? 'R' : 'W',
            addr);
}
