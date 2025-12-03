// matmul_bad.c
#define N 512

double A[N][N], B[N][N], C[N][N];

int main() {
    for (int k = 0; k < N; k++)
        for (int j = 0; j < N; j++)
            for (int i = 0; i < N; i++)
                C[i][j] += A[i][k] * B[k][j];  // awful locality for A and C
}
