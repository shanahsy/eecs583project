#include <stdio.h>

#define N (1 << 26) // 64 million ints (~256 MB)
static int A[N];

int main() {
    long long sum = 0;

    for (int i = 0; i < N; i++) {
        sum += A[i];
    }

    printf("%lld\n", sum);
    return 0;
}
