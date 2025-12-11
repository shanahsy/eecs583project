// strided_access_bad.c
#include <stddef.h>

#define N (1 << 24)     // ~16M elements
#define STRIDE 64

double A[N];

int main(void) {
    for (size_t i = 0; i < N; i++)
        A[i] = (double)i * 0.001;

    double sum = 0.0;
    // Large stride: touches one element per cache line, almost no reuse
    for (size_t i = 0; i < N; i += STRIDE)
        sum += A[i];

    return (sum > 0.0) ? 0 : 0;
}
