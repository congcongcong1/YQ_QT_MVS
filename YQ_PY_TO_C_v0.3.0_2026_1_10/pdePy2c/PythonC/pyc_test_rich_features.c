#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int g_seed = 12345;
int g_buf[] = { 1, 2, 3, 4, 5 };
int g_buf_len = 5;

uint32_t lcg_step(int x);
void fill_with_seq(int *arr, int n);
int sum_even(int *arr, int n);
void reverse_in_place(int *arr, int n);
int compare_prefix(int *a, int *b, int n_a, int n_b);
int test_ranges();
void user_main();

uint32_t lcg_step(int x) {
    return ((uint32_t)((uint64_t)(x) * 1664525u + 1013904223u));
}

void fill_with_seq(int *arr, int n) {
    int i;
    for (i = 0; i < n; i += 1) {
        arr[i] = (((i * 3) + 1) % 7);
    }
    return;
}

int sum_even(int *arr, int n) {
    int total = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        if (((arr[i] % 2) == 0)) {
            total = (total + arr[i]);
        }
    }
    return total;
}

void reverse_in_place(int *arr, int n) {
    int i = 0;
    int j = (n - 1);
    while ((i < j)) {
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
        i = (i + 1);
        j = (j - 1);
    }
    return;
}

int compare_prefix(int *a, int *b, int n_a, int n_b) {
    int na = n_a;
    int nb = n_b;
    int limit = na;
    if ((nb < limit)) {
        limit = nb;
    }
    int ok = 1;
    int i;
    for (i = 0; i < limit; i += 1) {
        if ((a[i] != b[i])) {
            ok = 0;
        }
    }
    return ok;
}

int test_ranges() {
    int total = 0;
    int i;
    for (i = 0; i < 5; i += 1) {
        total = (total + i);
    }
    for (i = 2; i < 7; i += 1) {
        total = (total + i);
    }
    for (i = 10; i > 0; i += (-2)) {
        total = (total + i);
    }
    return total;
}

void user_main() {
    char * s = "string_with_quote: \"ok\" and backslash \\\\";
    printf("%s %s\n", "String test:", s);
    int local[] = { 0, 0, 0, 0, 0 };
    int local_len = 5;
    fill_with_seq(local, local_len);
    printf("%s", "Filled: ");
    for (int __i_print = 0; __i_print < local_len; __i_print++) {
        printf("%d", local[__i_print]);
        if (__i_print < local_len - 1) printf(" ");
    }
    printf("\n");
    int even_sum = sum_even(local, local_len);
    printf("%s %d\n", "Even sum:", even_sum);
    reverse_in_place(local, local_len);
    printf("%s", "Reversed: ");
    for (int __i_print = 0; __i_print < local_len; __i_print++) {
        printf("%d", local[__i_print]);
        if (__i_print < local_len - 1) printf(" ");
    }
    printf("\n");
    int prefix_ok = compare_prefix(g_buf, local, g_buf_len, local_len);
    printf("%s %d\n", "Prefix ok:", prefix_ok);
    int rng_total = test_ranges();
    printf("%s %d\n", "Range total:", rng_total);
    uint32_t x = g_seed;
    int i;
    for (i = 0; i < 3; i += 1) {
        x = lcg_step(x);
    }
    printf("%s %u\n", "LCG last:", x);
    bool flag = ((even_sum > 5) && (rng_total > 10));
    if ((!flag)) {
        printf("Flag is false");
        printf("\n");
    } else {
        printf("Flag is true");
        printf("\n");
    }
    return;
}


void pyc_test_rich_features_pdePycProc() {
    printf("test_rich_features.py start");
    printf("\n");
    user_main();
    return;
}

int main() {
    pyc_test_rich_features_pdePycProc();
    return 0;
}