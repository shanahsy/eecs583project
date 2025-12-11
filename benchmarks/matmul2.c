// matmul_kji_bad.c
#include <stddef.h>

#define N 512

// Global arrays so LLVM IR is simple (no malloc indirection).
double A[N][N], B[N][N], C[N][N];

int main(void) {
    // Initialize matrices so work can't be trivially optimized away.
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            A[i][j] = (double)(i + j) * 0.001;
            B[i][j] = (double)(i - j) * 0.002;
            C[i][j] = 0.0;
        }
    }

    // Bad loop ordering for row-major: k, j, i
    // This gives poor locality for A and/or C.
    for (size_t k = 0; k < N; k++) {
        for (size_t j = 0; j < N; j++) {
            for (size_t i = 0; i < N; i++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    // Prevent dead-code elimination.
    double sum = 0.0;
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            sum += C[i][j];
        }
    }

    return (sum > 0.0) ? 0 : 0;
}
