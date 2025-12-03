// bad_matrix_walk.c
#include <stdio.h>

#define N 1024

int main() {
    static int A[N][N];
    long long sum = 0;

    for (int col = 0; col < N; col++) {
        for (int row = 0; row < N; row++) {
            sum += A[row][col];  
            // TERRIBLE: jumps 1024 ints between successive accesses
        }
    }

    printf("%lld\n", sum);
    return 0;
}
