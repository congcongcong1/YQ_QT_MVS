#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define MEM32(addr) (*(volatile uint32_t*)(addr))

int a = 10;
uint16_t b = 0x0F;
uint16_t c = 077;
double d = 3.5;
int flag = 1;
char * msg = "hello";
int arr[] = { 1, 2, 3, 4 };
int arr_len = 4;
int mat[] = { 1, 2, 3, 4, 5, 6 };
int mat_h = 3;
int mat_w = 2;
int machine = 0;

typedef struct {
    int x;
    int y;
} Point;

int clamp(int x, int lo, int hi);
int loop_test(int n);
int sum_array(int *x, int n);
int sum2d(int *x, int n, int w);
int mix_math(int x, int y);
int bit_mix(int x, int y);
int bool_ops(int x, int y);
int point_sum(Point p);
uint32_t lcg_step(int x);
int local_arr();
int mem_ops();

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

int loop_test(int n) {
    int s = 0;
    int i;
    for (i = 0; i < n; i += 2) {
        s += i;
    }
    int j;
    for (j = 5; j > 0; j += (-1)) {
        s += j;
    }
    int k = 0;
    while ((k < 10)) {
        k += 1;
        if (((k % 3) == 0)) {
            continue;
        }
        s += k;
        if ((s > 50)) {
            break;
        }
    }
    return s;
}

int sum_array(int *x, int n) {
    int total = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        total += x[i];
    }
    return total;
}

int sum2d(int *x, int n, int w) {
    int total = 0;
    int y;
    for (y = 0; y < n; y += 1) {
        int z;
        for (z = 0; z < w; z += 1) {
            total += x[(y) * (w) + (z)];
        }
    }
    return total;
}

int mix_math(int x, int y) {
    int add = (x + y);
    int sub = (x - y);
    int mul = (x * y);
    double div = ((double)(x) / (double)(y));
    int flo = ((int)floor((double)(x) / (double)(y)));
    int mod = (x % y);
    return (((((add + sub) + mul) + ((int)(div))) + flo) + mod);
}

int bit_mix(int x, int y) {
    int v = ((x << 4) | y);
    int m = (v & 0xFF);
    int n = (m ^ 0xAA);
    int p = (~n);
    return p;
}

int bool_ops(int x, int y) {
    bool t1 = ((x > y) && (x != 0));
    bool t2 = ((x < y) || (y == 0));
    int t3 = (!(x == y));
    if ((t1 && t2)) {
        return 1;
    }
    if (t3) {
        return 2;
    }
    return 0;
}

int point_sum(Point p) {
    return (p.x + p.y);
}

uint32_t lcg_step(int x) {
    return ((uint32_t)((uint64_t)(x) * 1664525u + 1013904223u));
}

int local_arr() {
    int x[] = { 5, 1, 4 };
    int x_len = 3;
    return sum_array(x, x_len);
}

int mem_ops() {
    if ((machine != 0)) {
        MEM32(0x40021018) |= (1 << 5);
        MEM32(0x40021018) &= (~0x20);
    }
    return 0;
}


void pyc_test_pdePycProc() {
    printf("msg %s\n", msg);
    printf("a");
    printf(" ");
    printf("%d", a);
    printf(" ");
    printf("b");
    printf(" ");
    printf("%u", (uint16_t)b);
    printf(" ");
    printf("c");
    printf(" ");
    printf("%u", (uint16_t)c);
    printf(" ");
    printf("d");
    printf(" ");
    printf("%2f\n", (double)d);
    printf("flag %d\n", flag);
    printf("arr ");
    for (int __i_print = 0; __i_print < arr_len; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < arr_len - 1) printf(" ");
    }
    printf("\n");
    printf("mat ");
    printf("[");
    for (int __y_print = 0; __y_print < mat_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < mat_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%d", mat[__y_print * mat_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    printf("mat[2][1] %d\n", mat[(2) * (mat_w) + (1)]);
    printf("loop_test %d\n", loop_test(12));
    printf("sum_array %d\n", sum_array(arr, arr_len));
    printf("sum2d %d\n", sum2d(mat, mat_h, mat_w));
    printf("mix_math %d\n", mix_math(9, 4));
    printf("bit_mix %d\n", bit_mix(b, 0xF0));
    printf("bool_ops %d\n", bool_ops(a, 0));
    Point p = (Point){3, 7};
    p.x = (p.x + 1);
    printf("point_sum %d\n", point_sum(p));
    printf("lcg_step %u\n", lcg_step(123456));
    printf("local_arr %d\n", local_arr());
    printf("mem_ops %d\n", mem_ops());
    return;
}

int main() {
    pyc_test_pdePycProc();
    return 0;
}