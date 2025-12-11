// matmul_bad_1k.c
#include <stdio.h>

#define N 1024

double A[N][N], B[N][N], C[N][N];

int main() {
    // Light init so compiler can't optimize everything away
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            A[i][j] = (i + j) * 0.001;
            B[i][j] = (i - j) * 0.002;
            C[i][j] = 0.0;
        }
    }

    // TERRIBLE order for A and C: k–j–i
    for (int k = 0; k < N; k++) {
        for (int j = 0; j < N; j++) {
            for (int i = 0; i < N; i++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    // Use result so loop isn't dead-code-eliminated
    double checksum = 0.0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            checksum += C[i][j];

    printf("checksum = %f\n", checksum);
    return 0;
}
