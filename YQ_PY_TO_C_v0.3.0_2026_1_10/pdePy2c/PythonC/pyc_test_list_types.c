#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

double prices[] = { 10.5, 20.0, 30.75 };
int prices_len = 3;
double matrix[] = { 1.1, 1.2, 2.1, 2.2 };
int matrix_h = 2;
int matrix_w = 2;

double sum_list(double * items, int n);

double sum_list(double * items, int n) {
    double s = 0.0;
    int i;
    for (i = 0; i < n; i += 1) {
        s += items[i];
    }
    return s;
}


void pyc_test_list_types_pdePycProc() {
    printf("%s %2f\n", "First price:", prices[0]);
    printf("%s %2f\n", "Matrix[1][0]:", matrix[(1) * (matrix_w) + (0)]);
    double total = sum_list(prices, prices_len);
    printf("%s %2f\n", "Total:", total);
    printf("Results:");
    printf(" ");
    printf("[");
    for (int __i_print = 0; __i_print < prices_len; __i_print++) {
        printf("%2f", prices[__i_print]);
        if (__i_print < prices_len - 1) printf(", ");
    }
    printf("]");
    printf(" ");
    printf("Total:");
    printf(" ");
    printf("%2f\n", total);
    printf("%s", "Matrix: ");
    printf("[");
    for (int __y_print = 0; __y_print < matrix_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < matrix_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%2f", matrix[__y_print * matrix_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    return;
}

int main() {
    pyc_test_list_types_pdePycProc();
    return 0;
}