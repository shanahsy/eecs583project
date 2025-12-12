// matmul_bad_rect.c
#include <stdio.h>

#define M 512   // rows of A, C
#define K 512   // inner dimension
#define N 256   // cols of B, C

double A[M][K], B[K][N], C[M][N];

int main() {
    // Init
    for (int i = 0; i < M; i++) {
        for (int k = 0; k < K; k++) {
            A[i][k] = (i + k) * 0.001;
        }
    }
    for (int k = 0; k < K; k++) {
        for (int j = 0; j < N; j++) {
            B[k][j] = (k - j) * 0.002;
        }
    }
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++)
            C[i][j] = 0.0;

    // Bad order again: k–j–i
    for (int k = 0; k < K; k++) {
        for (int j = 0; j < N; j++) {
            for (int i = 0; i < M; i++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    double checksum = 0.0;
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++)
            checksum += C[i][j];

    printf("checksum = %f\n", checksum);
    return 0;
}
