#include <stdlib.h>

#define MATRIX_SIZE 4096 

// Matrix transpose - poor cache locality
void transpose(int **matrix, int **result, int n) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            result[j][i] = matrix[i][j];
        }
    }
}

int main() {
    int **matrix = malloc(MATRIX_SIZE * sizeof(int *));
    int **result = malloc(MATRIX_SIZE * sizeof(int *));
    
    for (int i = 0; i < MATRIX_SIZE; i++) {
        matrix[i] = malloc(MATRIX_SIZE * sizeof(int));
        result[i] = malloc(MATRIX_SIZE * sizeof(int));
        for (int j = 0; j < MATRIX_SIZE; j++) {
            matrix[i][j] = i * MATRIX_SIZE + j;
        }
    }
    
    transpose(matrix, result, MATRIX_SIZE);
    
    for (int i = 0; i < MATRIX_SIZE; i++) {
        free(matrix[i]);
        free(result[i]);
    }
    free(matrix);
    free(result);
    
    return 0;
}