// linked_list_random.c
#include <stdlib.h>

#define N_NODES (1 << 20)   // 1M nodes
#define N_WALKS 5

typedef struct Node {
    struct Node *next;
    double value;
} Node;

Node *nodes;

static void shuffle_indices(int *idx, int n) {
    for (int i = n - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int tmp = idx[i];
        idx[i] = idx[j];
        idx[j] = tmp;
    }
}

int main(void) {
    nodes = (Node *)malloc(sizeof(Node) * N_NODES);
    if (!nodes) return 1;

    for (int i = 0; i < N_NODES; i++) {
        nodes[i].value = i * 0.001;
        nodes[i].next = NULL;
    }

    int *order = (int *)malloc(sizeof(int) * N_NODES);
    if (!order) return 1;
    for (int i = 0; i < N_NODES; i++)
        order[i] = i;

    srand(0);
    shuffle_indices(order, N_NODES);

    // Build a random permutation list
    for (int i = 0; i < N_NODES - 1; i++)
        nodes[order[i]].next = &nodes[order[i + 1]];
    nodes[order[N_NODES - 1]].next = NULL;

    Node *head = &nodes[order[0]];

    double sum = 0.0;
    for (int w = 0; w < N_WALKS; w++) {
        Node *p = head;
        while (p) {
            sum += p->value;
            p = p->next;
        }
    }

    free(order);
    free(nodes);
    return (sum > 0.0) ? 0 : 0;
}
