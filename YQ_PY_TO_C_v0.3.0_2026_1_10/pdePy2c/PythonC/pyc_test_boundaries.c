#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int g_counter = 0;
int g_data[] = { 10, 20, 30, 40, 50 };
int g_data_len = 5;

typedef struct {
    int x;
} MinimalStruct;

typedef struct {
    int a;
    int b;
    int c;
    int d;
    int e;
} LargeStruct;

void test_array_boundaries();
void test_nested_loops();
void test_nested_if();
int func_a(int x);
int func_b(int x);
int func_c(int x);
double func_d(int x);
void test_call_chain();
void test_complex_expressions();
void reverse_array(int *arr, int n);
void test_array_operations();
void test_2d_boundaries();
void test_control_flow_boundaries();
void test_numeric_boundaries();
void test_struct_boundaries();
void countdown(int n);
void test_limited_recursion();
void test_mixed_types();
void test_string_boundaries();
void increment_global();
int access_global_array();
void test_global_access();
int many_params(int a, int b, int c, int d, int e, int f, int g, int h);
void test_many_params();
void test_bitwise_boundaries();

void test_array_boundaries() {
    int arr1[] = { 1 };
    int arr1_len = 1;
    printf("%s %d\n", "Single element:", arr1[0]);
    int arr_large[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
    int arr_large_len = 20;
    int n = arr_large_len;
    int sum_val = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        sum_val += arr_large[i];
    }
    printf("%s %d\n", "Large array sum:", sum_val);
    return;
}

void test_nested_loops() {
    int count = 0;
    int i;
    for (i = 0; i < 3; i += 1) {
        int j;
        for (j = 0; j < 3; j += 1) {
            int k;
            for (k = 0; k < 3; k += 1) {
                count += 1;
            }
        }
    }
    printf("%s %d\n", "Triple nested count:", count);
    return;
}

void test_nested_if() {
    int x = 5;
    int y = 10;
    int z = 15;
    if ((x > 0)) {
        if ((y > 5)) {
            if ((z > 10)) {
                printf("All conditions met");
                printf("\n");
            }
        }
    }
    return;
}

int func_a(int x) {
    return (x + 1);
}

int func_b(int x) {
    return (func_a(x) * 2);
}

int func_c(int x) {
    return (func_b(x) - 3);
}

double func_d(int x) {
    return ((double)(func_c(x)) / (double)(2));
}

void test_call_chain() {
    double result = func_d(10);
    printf("%s %2f\n", "Call chain result:", result);
    return;
}

void test_complex_expressions() {
    int a = 5;
    int b = 10;
    int c = 15;
    double result1 = (((a + b) * c) - ((double)((a * b)) / (double)(c)));
    printf("%s %2f\n", "Complex expr 1:", result1);
    bool result2 = ((a < b) && (b < c) && ((a + b) < c));
    printf("%s %s\n", "Complex expr 2:", (result2 ? "True" : "False"));
    uint16_t x = 0xFF;
    uint16_t y = 0x0F;
    int result3 = (((x & y) | (x ^ y)) << 2);
    printf("%s %d\n", "Complex expr 3:", result3);
    return;
}

void reverse_array(int *arr, int n) {
    int i = 0;
    int j = (n - 1);
    while ((i < j)) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
        i += 1;
        j -= 1;
    }
    return;
}

void test_array_operations() {
    int data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    int data_len = 10;
    int n = data_len;
    printf("%s", "Before reverse: ");
    for (int __i_print = 0; __i_print < data_len; __i_print++) {
        printf("%d", data[__i_print]);
        if (__i_print < data_len - 1) printf(" ");
    }
    printf("\n");
    reverse_array(data, n);
    printf("%s", "After reverse: ");
    for (int __i_print = 0; __i_print < data_len; __i_print++) {
        printf("%d", data[__i_print]);
        if (__i_print < data_len - 1) printf(" ");
    }
    printf("\n");
    return;
}

void test_2d_boundaries() {
    int tiny[] = { 42 };
    int tiny_h = 1;
    int tiny_w = 1;
    printf("%s %d\n", "Tiny 2D:", tiny[(0) * (tiny_w) + (0)]);
    int mat[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    int mat_h = 3;
    int mat_w = 4;
    int h = mat_h;
    int w = mat_w;
    int y;
    for (y = 0; y < h; y += 1) {
        int x;
        for (x = 0; x < w; x += 1) {
            if (((x < h) && (y < w))) {
                int temp = mat[(y) * (mat_w) + (x)];
            }
        }
    }
    return;
}

void test_control_flow_boundaries() {
    int count = 0;
    int i;
    for (i = 0; i < 0; i += 1) {
        count += 1;
    }
    printf("%s %d\n", "Empty range count:", count);
    for (i = 0; i < 1; i += 1) {
        printf("%s %d\n", "Single iteration:", i);
    }
    int x = 0;
    while ((x > 0)) {
        x -= 1;
    }
    printf("%s %d\n", "While never entered:", x);
    for (i = 0; i < 100; i += 1) {
        break;
    }
    printf("Immediate break");
    printf("\n");
    return;
}

void test_numeric_boundaries() {
    int zero_int = 0;
    double zero_float = 0.0;
    printf("%s %d\n", "Zero int:", zero_int);
    printf("%s %2f\n", "Zero float:", zero_float);
    int neg_int = (-100);
    double neg_float = (-3.14);
    printf("%s %d\n", "Negative int:", neg_int);
    printf("%s %2f\n", "Negative float:", neg_float);
    int big_int = 999999;
    double big_float = 123456.789;
    printf("%s %d\n", "Big int:", big_int);
    printf("%s %2f\n", "Big float:", big_float);
    return;
}

void test_struct_boundaries() {
    MinimalStruct s1 = (MinimalStruct){42};
    printf("%s %d\n", "Minimal struct:", s1.x);
    LargeStruct s2 = (LargeStruct){1, 2, 3, 4, 5};
    int sum_val = ((((s2.a + s2.b) + s2.c) + s2.d) + s2.e);
    printf("%s %d\n", "Large struct sum:", sum_val);
    return;
}

void countdown(int n) {
    if ((n <= 0)) {
        printf("Done");
        printf("\n");
        return;
    }
    printf("%s %d\n", "Count:", n);
    countdown((n - 1));
    return;
}

void test_limited_recursion() {
    countdown(5);
    return;
}

void test_mixed_types() {
    int i = 10;
    double f = 3.5;
    double result1 = (i + f);
    printf("%s %2f\n", "int + float:", result1);
    double result2 = (i * f);
    printf("%s %2f\n", "int * float:", result2);
    double result3 = ((double)(i) / (double)(3));
    printf("%s %2f\n", "int / int:", result3);
    return;
}

void test_string_boundaries() {
    char * empty = "";
    printf("%s %s\n", "Empty string:", empty);
    char * single = "a";
    printf("%s %s\n", "Single char:", single);
    char * long_str = "This is a very long string for testing purposes with many characters";
    printf("%s %s\n", "Long string:", long_str);
    char * special = "Tab:\t Newline:\n Quote:\" Backslash:\\";
    printf("%s %s\n", "Special chars:", special);
    return;
}

void increment_global() {
    g_counter += 1;
    return;
}

int access_global_array() {
    int n = g_data_len;
    int sum_val = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        sum_val += g_data[i];
    }
    return sum_val;
}

void test_global_access() {
    increment_global();
    increment_global();
    increment_global();
    printf("%s %d\n", "Global counter:", g_counter);
    int total = access_global_array();
    printf("%s %d\n", "Global array sum:", total);
    return;
}

int many_params(int a, int b, int c, int d, int e, int f, int g, int h) {
    return (((((((a + b) + c) + d) + e) + f) + g) + h);
}

void test_many_params() {
    int result = many_params(1, 2, 3, 4, 5, 6, 7, 8);
    printf("%s %d\n", "Many params sum:", result);
    return;
}

void test_bitwise_boundaries() {
    uint16_t zero = 0x00;
    printf("%s %u\n", "All zeros:", zero);
    uint16_t all_ones = 0xFF;
    printf("%s %u\n", "All ones:", all_ones);
    int val = 1;
    int shifted = (val << 7);
    printf("%s %d\n", "Shift left 7:", shifted);
    int shifted_back = (shifted >> 7);
    printf("%s %d\n", "Shift right 7:", shifted_back);
    int inverted = (~0x00);
    printf("%s %d\n", "Inverted zero:", inverted);
    return;
}


void pyc_test_boundaries_pdePycProc() {
    printf("=== 边界条件测试开始 ===");
    printf("\n");
    printf("--- 测试1: 数组长度边界 ---");
    printf("\n");
    test_array_boundaries();
    printf("--- 测试2: 嵌套深度 ---");
    printf("\n");
    test_nested_loops();
    test_nested_if();
    printf("--- 测试3: 函数调用链 ---");
    printf("\n");
    test_call_chain();
    printf("--- 测试4: 复杂表达式 ---");
    printf("\n");
    test_complex_expressions();
    printf("--- 测试5: 数组操作边界 ---");
    printf("\n");
    test_array_operations();
    printf("--- 测试6: 二维数组边界 ---");
    printf("\n");
    test_2d_boundaries();
    printf("--- 测试7: 控制流边界 ---");
    printf("\n");
    test_control_flow_boundaries();
    printf("--- 测试8: 数值边界 ---");
    printf("\n");
    test_numeric_boundaries();
    printf("--- 测试9: 结构体边界 ---");
    printf("\n");
    test_struct_boundaries();
    printf("--- 测试10: 有限递归 ---");
    printf("\n");
    test_limited_recursion();
    printf("--- 测试11: 混合类型运算 ---");
    printf("\n");
    test_mixed_types();
    printf("--- 测试12: 字符串边界 ---");
    printf("\n");
    test_string_boundaries();
    printf("--- 测试13: 全局变量访问 ---");
    printf("\n");
    test_global_access();
    printf("--- 测试14: 多参数函数 ---");
    printf("\n");
    test_many_params();
    printf("--- 测试15: 位运算边界 ---");
    printf("\n");
    test_bitwise_boundaries();
    printf("=== 边界条件测试完成 ===");
    printf("\n");
    return;
}

int main() {
    pyc_test_boundaries_pdePycProc();
    return 0;
}