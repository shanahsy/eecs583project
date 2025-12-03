// false_sharing.c
#include <pthread.h>

#define N 4

typedef struct {
    int x;
} Item;

Item items[N];  // packed tightly → false sharing

void* worker(void *arg) {
    int id = (int)(long)arg;
    for (long i = 0; i < (1 << 27); i++) {
        items[id].x++;     // different threads hammer same cache line
    }
    return NULL;
}

int main() {
    pthread_t threads[N];

    for (int i = 0; i < N; i++)
        pthread_create(&threads[i], NULL, worker, (void*)(long)i);

    for (int i = 0; i < N; i++)
        pthread_join(threads[i], NULL);

    return 0;
}
