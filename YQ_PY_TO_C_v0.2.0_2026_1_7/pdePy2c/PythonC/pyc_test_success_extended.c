#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define MEM32(addr) (*(volatile uint32_t*)(addr))

int a = 10;
int b = 20;
double ratio = 1.5;
int cnt = 0;
int acc = 0;
int rev_sum = 0;
int arr[] = { 5, 1, 4, 2, 8 };
int arr_len = 5;
int n = 5;
int img[] = { 1, 2, 3, 4, 5, 6 };
int img_h = 2;
int img_w = 3;
char * s1 = "hello";
char * s2 = "world";
int machine = 0;

typedef struct {
    int x;
    int y;
} Point;

int clamp(int x, int lo, int hi);
int sum_array(int *_arg_arr, int _arg_n);
void bubble_sort(int *_arg_arr, int _arg_n);
int sum2d(int *_arg_arr, int _arg_n, int w);
int sum_point(Point pt);
int test_mcu_ops();

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

int sum2d(int *_arg_arr, int _arg_n, int w) {
    int s = 0;
    int y;
    for (y = 0; y < _arg_n; y += 1) {
        int x;
        for (x = 0; x < w; x += 1) {
            s += _arg_arr[(y) * (w) + (x)];
        }
    }
    return s;
}

int sum_point(Point pt) {
    return (pt.x + pt.y);
}

int test_mcu_ops() {
    uint16_t a1 = 0x0F;
    uint16_t b1 = 0xF0;
    int c1 = ((a1 << 4) | b1);
    int d1 = (c1 & 0xFF);
    int e1 = (d1 ^ 0xAA);
    int f1 = (~e1);
    printf("mcu_a %u\n", a1);
    printf("mcu_b %u\n", b1);
    printf("mcu_c %d\n", c1);
    printf("mcu_d %d\n", d1);
    printf("mcu_e %d\n", e1);
    printf("mcu_f %d\n", f1);
    if ((machine != 0)) {
        MEM32(0x40021018) |= (1 << 5);
        MEM32(0x40021018) &= (~0x20);
    }
    return f1;
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
    printf("img ");
    printf("[");
    for (int __y_print = 0; __y_print < img_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < img_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%d", img[__y_print * img_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    printf("img[1][2] %d\n", img[(1) * (img_w) + (2)]);
    printf("sum2d %d\n", sum2d(img, img_h, img_w));
    Point p = (Point){3, 4};
    printf("p.x %d\n", p.x);
    p.x = (p.x + 1);
    printf("p.x2 %d\n", p.x);
    printf("sum_point %d\n", sum_point(p));
    printf("s1 %s\n", s1);
    printf("s2 %s\n", s2);
    printf("mcu_ops %d\n", test_mcu_ops());
    return;
}

int main() {
    pyc_test_success_extended_pdePycProc();
    return 0;
}