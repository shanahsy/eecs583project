// bad_linked_list.c
#include <stdlib.h>

typedef struct Node {
    int val;
    struct Node *next;
} Node;

int main() {
    const int N = 200000;
    Node *head = NULL;

    // Allocate nodes randomly scattered in heap
    for (int i = 0; i < N; i++) {
        Node *n = malloc(sizeof(Node));
        n->val = i;
        n->next = head;
        head = n;
    }

    long long sum = 0;
    while (head) {
        sum += head->val;   // pointer chasing → bad locality
        head = head->next;
    }

    return (int)sum;
}
