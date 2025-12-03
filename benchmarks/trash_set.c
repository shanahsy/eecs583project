// thrash_set.c
#include <stdlib.h>

#define N 8 * 1024           // enough to exceed L1 capacity
#define STRIDE 64           // one cache line

int main() {
    char *a = malloc(N);

    // Thrash: cycle through many lines mapping to the same set (if powers of two)
    for (int i = 0; i < 100000; i++) {
        for (int off = 0; off < N; off += STRIDE) {
            a[off]++;
        }
    }
}
