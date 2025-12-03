// stride_killer.c
#include <stdio.h>
#include <stdlib.h>

#define N (1 << 20)      // 1M ints
#define STRIDE 1024      // really bad stride

int main() {
    int *a = malloc(N * sizeof(int));
    long long sum = 0;

    for (int i = 0; i < N; i += STRIDE) {
        sum += a[i];   // each load hits a different cache line
    }

    printf("%lld\n", sum);
    return 0;
}
