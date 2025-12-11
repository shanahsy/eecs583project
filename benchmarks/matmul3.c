// matmul_ikj_bad.c
#include <stddef.h>

#define N 512

double A[N][N], B[N][N], C[N][N];

int main(void) {
    // Initialize matrices
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            A[i][j] = (double)(i + j) * 0.001;
            B[i][j] = (double)(i - j) * 0.002;
            C[i][j] = 0.0;
        }
    }

    // Ordering: i, k, j
    // Good for A's rows, worse for B[k][j] depending on cache.
    for (size_t i = 0; i < N; i++) {
        for (size_t k = 0; k < N; k++) {
            for (size_t j = 0; j < N; j++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    // Consume result
    double sum = 0.0;
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            sum += C[i][j];
        }
    }

    return (sum > 0.0) ? 0 : 0;
}
