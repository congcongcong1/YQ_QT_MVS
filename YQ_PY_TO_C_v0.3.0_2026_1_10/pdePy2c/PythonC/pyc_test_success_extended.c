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
    printf("%s %u\n", "mcu_a", a1);
    printf("%s %u\n", "mcu_b", b1);
    printf("%s %d\n", "mcu_c", c1);
    printf("%s %d\n", "mcu_d", d1);
    printf("%s %d\n", "mcu_e", e1);
    printf("%s %d\n", "mcu_f", f1);
    if ((machine != 0)) {
        MEM32(0x40021018) |= (1 << 5);
        MEM32(0x40021018) &= (~0x20);
    }
    return f1;
}


void pyc_test_success_extended_pdePycProc() {
    printf("test_success_extended.py");
    printf("\n");
    int result = ((a + b) * 2);
    double mixed = (result * ratio);
    bool ok = ((result > 50) && (a < b));
    printf("%s %d\n", "result", result);
    printf("%s %2f\n", "mixed", mixed);
    printf("%s %s\n", "ok", (ok ? "True" : "False"));
    if ((result > 100)) {
        printf("%s %d\n", "huge", result);
    } else {
        if ((result > 50)) {
            printf("%s %d\n", "large", result);
        } else {
            printf("%s %d\n", "small", result);
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
    printf("%s %d\n", "acc", acc);
    int k;
    for (k = 5; k > 0; k += (-1)) {
        rev_sum += k;
    }
    printf("%s %d\n", "rev_sum", rev_sum);
    printf("%s", "before sort: ");
    for (int __i_print = 0; __i_print < arr_len; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < arr_len - 1) printf(" ");
    }
    printf("\n");
    bubble_sort(arr, n);
    printf("%s", "after sort: ");
    for (int __i_print = 0; __i_print < arr_len; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < arr_len - 1) printf(" ");
    }
    printf("\n");
    int total = sum_array(arr, arr_len);
    printf("%s %d\n", "sum", total);
    int c = clamp(total, 0, 20);
    printf("%s %d\n", "clamp", c);
    printf("%s", "img ");
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
    printf("%s %d\n", "img[1][2]", img[(1) * (img_w) + (2)]);
    printf("%s %d\n", "sum2d", sum2d(img, img_h, img_w));
    Point p = (Point){3, 4};
    printf("%s %d\n", "p.x", p.x);
    p.x = (p.x + 1);
    printf("%s %d\n", "p.x2", p.x);
    printf("%s %d\n", "sum_point", sum_point(p));
    printf("%s %s\n", "s1", s1);
    printf("%s %s\n", "s2", s2);
    printf("%s %d\n", "mcu_ops", test_mcu_ops());
    return;
}

int main() {
    pyc_test_success_extended_pdePycProc();
    return 0;
}