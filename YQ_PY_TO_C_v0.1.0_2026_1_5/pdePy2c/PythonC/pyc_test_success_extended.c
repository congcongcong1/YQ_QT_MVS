#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int a = 10;
int b = 20;
double ratio = 1.5;
int cnt = 0;
int acc = 0;
int rev_sum = 0;
int arr[] = { 5, 1, 4, 2, 8 };
int arr_len = 5;
int n = 5;

int clamp(int x, int lo, int hi);
int sum_array(int *_arg_arr, int _arg_n);
void bubble_sort(int *_arg_arr, int _arg_n);

int clamp(int x, int lo, int hi) {
    if ((x < lo)) {
        return lo;
    } else {
        if ((x > hi)) {
            return hi;
        } else {
            return x;
        }
    }
    return 0;
}

int sum_array(int *_arg_arr, int _arg_n) {
    int s = 0;
    int i;
    for (i = 0; i < _arg_n; i += 1) {
        s += _arg_arr[i];
    }
    return s;
}

void bubble_sort(int *_arg_arr, int _arg_n) {
    int i;
    for (i = 0; i < (_arg_n - 1); i += 1) {
        int swapped = 0;
        int j;
        for (j = 0; j < ((_arg_n - 1) - i); j += 1) {
            if ((_arg_arr[j] > _arg_arr[(j + 1)])) {
                int temp = _arg_arr[j];
                _arg_arr[j] = _arg_arr[(j + 1)];
                _arg_arr[(j + 1)] = temp;
                swapped = 1;
            }
        }
        if ((swapped == 0)) {
            return;
        }
    }
    return;
}


void pyc_test_success_extended_pdePycProc() {
    printf("test_success_extended.py\n");
    int result = ((a + b) * 2);
    double mixed = (result * ratio);
    bool ok = ((result > 50) && (a < b));
    printf("result %d\n", result);
    printf("mixed %2f\n", mixed);
    printf("ok %s\n", (ok ? "True" : "False"));
    if ((result > 100)) {
        printf("huge %d\n", result);
    } else {
        if ((result > 50)) {
            printf("large %d\n", result);
        } else {
            printf("small %d\n", result);
        }
    }
    while ((cnt < 10)) {
        cnt += 1;
        if (((cnt % 2) == 0)) {
            continue;
        }
        acc += cnt;
        if ((acc > 20)) {
            break;
        }
    }
    printf("acc %d\n", acc);
    int k;
    for (k = 5; k > 0; k += (-1)) {
        rev_sum += k;
    }
    printf("rev_sum %d\n", rev_sum);
    printf("before sort: ");
    for (int __i_print = 0; __i_print < arr_len; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < arr_len - 1) printf(" ");
    }
    printf("\n");
    bubble_sort(arr, n);
    printf("after sort: ");
    for (int __i_print = 0; __i_print < arr_len; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < arr_len - 1) printf(" ");
    }
    printf("\n");
    int total = sum_array(arr, arr_len);
    printf("sum %d\n", total);
    int c = clamp(total, 0, 20);
    printf("clamp %d\n", c);
    return;
}

int main() {
    pyc_test_success_extended_pdePycProc();
    return 0;
}