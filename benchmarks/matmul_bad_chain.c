// matmul_bad_chain.c
#include <stdio.h>

#define N 512
#define R 3  // number of repeated matmuls

double A[N][N], B[N][N], C[N][N];

int main() {
    // Initialize A and B, zero C
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = (i + j) * 0.001;
            B[i][j] = (i - j) * 0.002;
            C[i][j] = 0.0;
        }
    }

    for (int r = 0; r < R; r++) {
        for (int k = 0; k < N; k++) {
            for (int j = 0; j < N; j++) {
                for (int i = 0; i < N; i++) {
                    C[i][j] += A[i][k] * B[k][j];  // still awful for A and C
                }
            }
        }
    }

    double checksum = 0.0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            checksum += C[i][j];

    printf("checksum = %f\n", checksum);
    return 0;
}
