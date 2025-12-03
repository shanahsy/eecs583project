// bad_hashtable.c
#define N 1000000
int table[N];

int hash(int x) { return 0; }  // every key collides lol

int main() {
    for (int i = 0; i < N; i++)
        table[hash(i)] = i;   // same location → conflict misses amplified
}
