// random_access.c
#include <stdlib.h>
#include <time.h>

#define N (1 << 20)

int main() {
    int *a = malloc(N * sizeof(int));

    srand(0);
    long long sum = 0;

    for (int i = 0; i < N; i++) {
        int idx = rand() % N;
        sum += a[idx];
        // basically guaranteed cache miss unless you get lucky
    }
    return sum;
}
