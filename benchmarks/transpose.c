// transpose_flat_bad.c
#include <stddef.h>

#define N 2048

double A[N][N], B[N][N];

int main(void) {
    // Initialize A
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            A[i][j] = (double)(i * N + j);
        }
    }

    // Naive transpose with poor locality for B:
    // Inner loop walks "down" a column in row-major storage.
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            B[j][i] = A[i][j];
        }
    }

    // Consume B to keep it live.
    double sum = 0.0;
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            sum += B[i][j];
        }
    }

    return (sum > 0.0) ? 0 : 0;
}
