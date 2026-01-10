#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int g_int = 100;
double g_float = 2.718;
int g_array[] = { 1, 2, 3, 4, 5 };
int g_array_len = 5;
int g_array2d[] = { 10, 20, 30, 40, 50, 60 };
int g_array2d_h = 3;
int g_array2d_w = 2;

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    int left;
    int top;
    int width;
    int height;
} Rectangle;

int test_basic_types();
int test_globals();
int test_1d_array_read(int *arr, int n);
void test_1d_array_write(int *arr, int n);
void test_1d_array_local();
int test_2d_array_read(int *arr, int h, int w, int n);
void test_2d_array_write(int *arr, int h, int w, int n);
void test_2d_array_local();
int test_if_elif_else(int x);
void test_for_range();
void test_while_loop();
void test_break_continue();
int add(int a, int b);
int multiply(int a, int b);
int factorial(int n);
int fibonacci(int n);
void test_struct();
int distance_squared(Point p1, Point p2);
void test_struct_param();
void test_arithmetic();
void test_comparison();
void test_logical();
void test_bitwise();
void test_len_function();
void test_print_variations();
void test_uint_types();
void bubble_sort(int *arr, int n);
int binary_search(int *arr, int n, int target);
void test_algorithms();
void test_edge_cases();
void test_type_conversion();

int test_basic_types() {
    int i1 = 42;
    int i2 = (-10);
    int i3 = 0;
    printf("int:");
    printf(" ");
    printf("%d", i1);
    printf(" ");
    printf("%d", i2);
    printf(" ");
    printf("%d\n", i3);
    double f1 = 3.14;
    double f2 = (-2.5);
    double f3 = 0.0;
    printf("float:");
    printf(" ");
    printf("%2f", f1);
    printf(" ");
    printf("%2f", f2);
    printf(" ");
    printf("%2f\n", f3);
    int b1 = 1;
    int b2 = 0;
    printf("bool:");
    printf(" ");
    printf("%d", b1);
    printf(" ");
    printf("%d\n", b2);
    char * s1 = "hello";
    char * s2 = "world";
    char * s3 = "test \"quote\" and backslash \\";
    printf("%s %s\n", "str:", s1);
    printf("%s %s\n", "str:", s2);
    printf("%s %s\n", "str:", s3);
    return 1;
}

int test_globals() {
    printf("%s %d\n", "g_int:", g_int);
    printf("%s %2f\n", "g_float:", g_float);
    printf("%s", "g_array: ");
    for (int __i_print = 0; __i_print < g_array_len; __i_print++) {
        printf("%d", g_array[__i_print]);
        if (__i_print < g_array_len - 1) printf(" ");
    }
    printf("\n");
    printf("%s", "g_array2d: ");
    printf("[");
    for (int __y_print = 0; __y_print < g_array2d_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < g_array2d_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%d", g_array2d[__y_print * g_array2d_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    return g_int;
}

int test_1d_array_read(int *arr, int n) {
    int sum_val = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        sum_val += arr[i];
    }
    printf("%s %d\n", "1D array sum:", sum_val);
    return sum_val;
}

void test_1d_array_write(int *arr, int n) {
    int i;
    for (i = 0; i < n; i += 1) {
        arr[i] = (i * 2);
    }
    printf("%s", "1D array modified: ");
    for (int __i_print = 0; __i_print < n; __i_print++) {
        printf("%d", arr[__i_print]);
        if (__i_print < n - 1) printf(" ");
    }
    printf("\n");
    return;
}

void test_1d_array_local() {
    int local[] = { 7, 8, 9 };
    int local_len = 3;
    printf("%s", "Local array: ");
    for (int __i_print = 0; __i_print < local_len; __i_print++) {
        printf("%d", local[__i_print]);
        if (__i_print < local_len - 1) printf(" ");
    }
    printf("\n");
    test_1d_array_read(local, local_len);
    test_1d_array_write(local, local_len);
    printf("%s", "After write: ");
    for (int __i_print = 0; __i_print < local_len; __i_print++) {
        printf("%d", local[__i_print]);
        if (__i_print < local_len - 1) printf(" ");
    }
    printf("\n");
    return;
}

int test_2d_array_read(int *arr, int h, int w, int n) {
    int sum_val = 0;
    int y;
    for (y = 0; y < h; y += 1) {
        int x;
        for (x = 0; x < w; x += 1) {
            sum_val += arr[(y) * (w) + (x)];
        }
    }
    printf("%s %d\n", "2D array sum:", sum_val);
    return sum_val;
}

void test_2d_array_write(int *arr, int h, int w, int n) {
    int y;
    for (y = 0; y < h; y += 1) {
        int x;
        for (x = 0; x < w; x += 1) {
            arr[(y) * (w) + (x)] = ((y * 10) + x);
        }
    }
    return;
}

void test_2d_array_local() {
    int local2d[] = { 1, 2, 3, 4, 5, 6 };
    int local2d_h = 2;
    int local2d_w = 3;
    int h = 2;
    int w = 3;
    printf("%s", "Local 2D array: ");
    printf("[");
    for (int __y_print = 0; __y_print < local2d_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < local2d_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%d", local2d[__y_print * local2d_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    test_2d_array_read(local2d, h, w, local2d_h);
    test_2d_array_write(local2d, h, w, local2d_h);
    printf("%s", "After write: ");
    printf("[");
    for (int __y_print = 0; __y_print < local2d_h; __y_print++) {
        if (__y_print > 0) printf(", ");
        printf("[");
        for (int __x_print = 0; __x_print < local2d_w; __x_print++) {
            if (__x_print > 0) printf(", ");
            printf("%d", local2d[__y_print * local2d_w + __x_print]);
        }
        printf("]");
    }
    printf("]\n");
    printf("%s %d\n", "Element [1][2]:", local2d[(1) * (local2d_w) + (2)]);
    return;
}

int test_if_elif_else(int x) {
    if ((x < 0)) {
        printf("negative");
        printf("\n");
        return (-1);
    } else {
        if ((x == 0)) {
            printf("zero");
            printf("\n");
            return 0;
        } else {
            printf("positive");
            printf("\n");
            return 1;
        }
    }
    return 0;
}

void test_for_range() {
    int sum1 = 0;
    int i;
    for (i = 0; i < 5; i += 1) {
        sum1 += i;
    }
    printf("%s %d\n", "range(5) sum:", sum1);
    int sum2 = 0;
    for (i = 2; i < 7; i += 1) {
        sum2 += i;
    }
    printf("%s %d\n", "range(2,7) sum:", sum2);
    int sum3 = 0;
    for (i = 0; i < 10; i += 2) {
        sum3 += i;
    }
    printf("%s %d\n", "range(0,10,2) sum:", sum3);
    int sum4 = 0;
    for (i = 10; i > 0; i += (-1)) {
        sum4 += i;
    }
    printf("%s %d\n", "range(10,0,-1) sum:", sum4);
    return;
}

void test_while_loop() {
    int count = 0;
    int total = 0;
    while ((count < 5)) {
        total += count;
        count += 1;
    }
    printf("%s %d\n", "while sum:", total);
    return;
}

void test_break_continue() {
    int sum_break = 0;
    int i;
    for (i = 0; i < 100; i += 1) {
        if ((i >= 5)) {
            break;
        }
        sum_break += i;
    }
    printf("%s %d\n", "break sum:", sum_break);
    int sum_continue = 0;
    for (i = 0; i < 10; i += 1) {
        if (((i % 2) == 0)) {
            continue;
        }
        sum_continue += i;
    }
    printf("%s %d\n", "continue sum:", sum_continue);
    return;
}

int add(int a, int b) {
    return (a + b);
}

int multiply(int a, int b) {
    return (a * b);
}

int factorial(int n) {
    if ((n <= 1)) {
        return 1;
    }
    int result = 1;
    int i;
    for (i = 2; i < (n + 1); i += 1) {
        result *= i;
    }
    return result;
}

int fibonacci(int n) {
    if ((n <= 1)) {
        return n;
    }
    int a = 0;
    int b = 1;
    int i;
    for (i = 2; i < (n + 1); i += 1) {
        int c = (a + b);
        a = b;
        b = c;
    }
    return b;
}

void test_struct() {
    Point p1 = (Point){10, 20};
    int px = p1.x;
    int py = p1.y;
    printf("%s %d\n", "Point x:", px);
    printf("%s %d\n", "Point y:", py);
    p1.x = 30;
    p1.y = 40;
    int px2 = p1.x;
    int py2 = p1.y;
    printf("%s %d\n", "Modified x:", px2);
    printf("%s %d\n", "Modified y:", py2);
    Rectangle r1 = (Rectangle){0, 0, 100, 50};
    int rw = r1.width;
    int rh = r1.height;
    printf("%s %d\n", "Rect width:", rw);
    printf("%s %d\n", "Rect height:", rh);
    return;
}

int distance_squared(Point p1, Point p2) {
    int x1 = p1.x;
    int y1 = p1.y;
    int x2 = p2.x;
    int y2 = p2.y;
    int dx = (x1 - x2);
    int dy = (y1 - y2);
    int dist_sq = ((dx * dx) + (dy * dy));
    return dist_sq;
}

void test_struct_param() {
    Point pa = (Point){0, 0};
    Point pb = (Point){3, 4};
    int dist_sq = distance_squared(pa, pb);
    printf("%s %d\n", "Distance squared:", dist_sq);
    return;
}

void test_arithmetic() {
    int a = 10;
    int b = 3;
    printf("%s %d\n", "a + b:", (a + b));
    printf("%s %d\n", "a - b:", (a - b));
    printf("%s %d\n", "a * b:", (a * b));
    printf("%s %2f\n", "a / b:", ((double)(a) / (double)(b)));
    printf("%s %d\n", "a % b:", (a % b));
    int c = 5;
    c += 2;
    printf("%s %d\n", "c += 2:", c);
    c -= 1;
    printf("%s %d\n", "c -= 1:", c);
    c *= 3;
    printf("%s %d\n", "c *= 3:", c);
    return;
}

void test_comparison() {
    int x = 5;
    int y = 10;
    printf("%s %s\n", "x < y:", ((x < y) ? "True" : "False"));
    printf("%s %s\n", "x <= y:", ((x <= y) ? "True" : "False"));
    printf("%s %s\n", "x > y:", ((x > y) ? "True" : "False"));
    printf("%s %s\n", "x >= y:", ((x >= y) ? "True" : "False"));
    printf("%s %s\n", "x == y:", ((x == y) ? "True" : "False"));
    printf("%s %s\n", "x != y:", ((x != y) ? "True" : "False"));
    return;
}

void test_logical() {
    int a = 1;
    int b = 0;
    printf("%s %s\n", "a and b:", ((a && b) ? "True" : "False"));
    printf("%s %s\n", "a or b:", ((a || b) ? "True" : "False"));
    printf("%s %d\n", "not a:", (!a));
    printf("%s %d\n", "not b:", (!b));
    return;
}

void test_bitwise() {
    uint16_t x = 0x0F;
    uint16_t y = 0xF0;
    printf("%s %d\n", "x & y:", (x & y));
    printf("%s %d\n", "x | y:", (x | y));
    printf("%s %d\n", "x ^ y:", (x ^ y));
    printf("%s %d\n", "~x:", (~x));
    printf("%s %d\n", "x << 2:", (x << 2));
    printf("%s %d\n", "y >> 4:", (y >> 4));
    return;
}

void test_len_function() {
    int arr1[] = { 1, 2, 3, 4, 5 };
    int arr1_len = 5;
    int n1 = arr1_len;
    printf("%s %d\n", "len(arr1):", n1);
    return;
}

void test_print_variations() {
    printf("\n");
    printf("%d\n", 123);
    printf("%2f\n", 4.56);
    printf("text");
    printf("\n");
    printf("%s %d\n", "value:", 789);
    printf("a");
    printf(" ");
    printf("b");
    printf(" ");
    printf("c");
    printf("\n");
    printf("x:");
    printf(" ");
    printf("%d", 1);
    printf(" ");
    printf("y:");
    printf(" ");
    printf("%d\n", 2);
    return;
}

void test_uint_types() {
    uint32_t val32 = ((uint32_t)((123456789 * 2)));
    printf("%s %u\n", "uint32:", val32);
    uint16_t val16 = 0xABCD;
    printf("%s %u\n", "uint16:", val16);
    return;
}

void bubble_sort(int *arr, int n) {
    int i;
    for (i = 0; i < (n - 1); i += 1) {
        int j;
        for (j = 0; j < ((n - 1) - i); j += 1) {
            if ((arr[j] > arr[(j + 1)])) {
                int temp = arr[j];
                arr[j] = arr[(j + 1)];
                arr[(j + 1)] = temp;
            }
        }
    }
    return;
}

int binary_search(int *arr, int n, int target) {
    int left = 0;
    int right = (n - 1);
    while ((left <= right)) {
        double mid = ((double)((left + right)) / (double)(2));
        int mid_int = ((int)(mid));
        if ((arr[mid_int] == target)) {
            return mid_int;
        } else {
            if ((arr[mid_int] < target)) {
                left = (mid_int + 1);
            } else {
                right = (mid_int - 1);
            }
        }
    }
    return (-1);
}

void test_algorithms() {
    int data[] = { 5, 2, 8, 1, 9, 3, 7, 4, 6 };
    int data_len = 9;
    int n = data_len;
    printf("%s", "Before sort: ");
    for (int __i_print = 0; __i_print < data_len; __i_print++) {
        printf("%d", data[__i_print]);
        if (__i_print < data_len - 1) printf(" ");
    }
    printf("\n");
    bubble_sort(data, n);
    printf("%s", "After sort: ");
    for (int __i_print = 0; __i_print < data_len; __i_print++) {
        printf("%d", data[__i_print]);
        if (__i_print < data_len - 1) printf(" ");
    }
    printf("\n");
    int target = 7;
    int index = binary_search(data, n, target);
    printf("%s %d\n", "Search 7:", index);
    return;
}

void test_edge_cases() {
    int empty[] = {  };
    int empty_len = 0;
    int n_empty = empty_len;
    printf("%s %d\n", "empty len:", n_empty);
    int single[] = { 42 };
    int single_len = 1;
    printf("%s", "single: ");
    for (int __i_print = 0; __i_print < single_len; __i_print++) {
        printf("%d", single[__i_print]);
        if (__i_print < single_len - 1) printf(" ");
    }
    printf("\n");
    int i;
    for (i = 0; i < 3; i += 1) {
        int j;
        for (j = 0; j < 3; j += 1) {
            if ((i == j)) {
                continue;
            }
            if (((i + j) > 3)) {
                break;
            }
            printf("i,j:");
            printf(" ");
            printf("%d", i);
            printf(" ");
            printf("%d\n", j);
        }
    }
    int x = 5;
    if ((x > 0)) {
        if ((x < 10)) {
            if ((x == 5)) {
                printf("x is 5");
                printf("\n");
            }
        }
    }
    return;
}

void test_type_conversion() {
    int i = 10;
    double f = ((double)(i) / (double)(3));
    printf("%s %2f\n", "int to float:", f);
    double f2 = 3.7;
    int i2 = ((int)(f2));
    printf("%s %d\n", "float to int:", i2);
    int b = 1;
    int i3 = (b + 5);
    printf("%s %d\n", "bool to int:", i3);
    return;
}


void pyc_test_comprehensive_pdePycProc() {
    printf("=== 综合测试开始 ===");
    printf("\n");
    printf("--- 测试1: 基础数据类型 ---");
    printf("\n");
    test_basic_types();
    printf("--- 测试2: 全局变量 ---");
    printf("\n");
    test_globals();
    printf("--- 测试3: 一维数组 ---");
    printf("\n");
    test_1d_array_local();
    printf("--- 测试4: 二维数组 ---");
    printf("\n");
    test_2d_array_local();
    printf("--- 测试5: 控制流 ---");
    printf("\n");
    test_if_elif_else(5);
    test_if_elif_else(0);
    test_if_elif_else((-3));
    test_for_range();
    test_while_loop();
    test_break_continue();
    printf("--- 测试6: 函数 ---");
    printf("\n");
    printf("%s %d\n", "add(3,4):", add(3, 4));
    printf("%s %d\n", "multiply(5,6):", multiply(5, 6));
    printf("%s %d\n", "factorial(5):", factorial(5));
    printf("%s %d\n", "fibonacci(10):", fibonacci(10));
    printf("--- 测试7: 结构体 ---");
    printf("\n");
    test_struct();
    test_struct_param();
    printf("--- 测试8: 运算符 ---");
    printf("\n");
    test_arithmetic();
    test_comparison();
    test_logical();
    test_bitwise();
    printf("--- 测试9: 特殊功能 ---");
    printf("\n");
    test_len_function();
    test_print_variations();
    test_uint_types();
    printf("--- 测试10: 综合算法 ---");
    printf("\n");
    test_algorithms();
    printf("--- 测试11: 边界情况 ---");
    printf("\n");
    test_edge_cases();
    printf("--- 测试12: 类型转换 ---");
    printf("\n");
    test_type_conversion();
    printf("=== 综合测试完成 ===");
    printf("\n");
    return;
}

int main() {
    pyc_test_comprehensive_pdePycProc();
    return 0;
}