#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int G_OFFSET = 100;
uint32_t G_MASK = 0xFFFFFFFFU;
double G_SCALES[] = { 1.0, 1.5, 2.25 };
int G_SCALES_len = 3;
double G_MATRIX[] = { 0.1, 0.2, 0.3, 0.4, 0.5, 0.6 };
int G_MATRIX_h = 3;
int G_MATRIX_w = 2;

typedef struct {
    double x;
    double y;
    double z;
} Vector3;

typedef struct {
    double kp;
    double ki;
    double kd;
    double integral;
    double last_error;
} PIDController;

double calculate_norm(Vector3 v);
double process_signal(double * samples, double gain, int n);
int lcg_step(int seed);
void test_ultimate_features();

double calculate_norm(Vector3 v) {
    return sqrt((((v.x * v.x) + (v.y * v.y)) + (v.z * v.z)));
}

double process_signal(double * samples, double gain, int n) {
    double total = 0.0;
    int i;
    for (i = 0; i < n; i += 1) {
        samples[i] = (samples[i] * gain);
        total += samples[i];
    }
    return ((double)(total) / (double)(n));
}

int lcg_step(int seed) {
    return ((uint32_t)((uint64_t)(seed) * 1664525u + 1013904223u));
}

void test_ultimate_features() {
    printf("=== 开始万无一失究极测试 ===");
    printf("\n");
    printf("--- A. 算术与除法精度 ---");
    printf("\n");
    int a = 10;
    int b = 3;
    double div_res = ((double)(a) / (double)(b));
    int floor_res = ((int)floor((double)(a) / (double)(b)));
    printf("%s %2f\n", "Float Div (10/3):", div_res);
    printf("%s %d\n", "Floor Div (10//3):", floor_res);
    printf("--- B. 列表与 2D 数组 ---");
    printf("\n");
    double data[] = { 1.2, 3.4, 5.6 };
    int data_len = 3;
    printf("%s", "1D samples: ");
    for (int __i_print = 0; __i_print < data_len; __i_print++) {
        printf("%2f", data[__i_print]);
        if (__i_print < data_len - 1) printf(" ");
    }
    printf("\n");
    double avg = process_signal(data, 2.0, data_len);
    printf("%s %2f\n", "Signal Mean after Gain:", avg);
    printf("%s %2f\n", "Matrix [1][1]:", G_MATRIX[(1) * (G_MATRIX_w) + (1)]);
    printf("%s %2f\n", "Matrix [2][1]:", G_MATRIX[(2) * (G_MATRIX_w) + (1)]);
    printf("--- C. 结构体与参数传递 ---");
    printf("\n");
    Vector3 v = (Vector3){1.0, 2.0, 3.0};
    double norm = calculate_norm(v);
    printf("%s %2f\n", "Vector Norm:", norm);
    PIDController pid = (PIDController){1.5, 0.1, 0.05};
    pid.integral += 1.2;
    printf("PID Kp:");
    printf(" ");
    printf("%2f", pid.kp);
    printf(" ");
    printf("Integral:");
    printf(" ");
    printf("%2f\n", pid.integral);
    printf("--- D. 控制流与布尔逻辑 ---");
    printf("\n");
    int count = 0;
    int i;
    for (i = 0; i < 10; i += 1) {
        if (((i % 2) == 0)) {
            continue;
        }
        if ((i > 7)) {
            break;
        }
        count += 1;
    }
    bool status = (count == 4);
    printf("%s %s\n", "Loop Status (count=4?):", (status ? "True" : "False"));
    if ((strcmp("test", "test") == 0)) {
        printf("String equality works.");
        printf("\n");
    }
    printf("--- E. 位运算与大整数 ---");
    printf("\n");
    int u_val = 2147483647;
    int u_next = lcg_step(u_val);
    printf("%s %d\n", "LCG sequence next:", u_next);
    uint16_t flags = 0xAA;
    uint16_t mask = 0x0F;
    printf("%s %d\n", "Bitwise AND:", (flags & mask));
    printf("%s %d\n", "Bitwise OR:", (flags | mask));
    printf("--- F. 数学库 ---");
    printf("\n");
    double val = 0.5;
    double s_val = sin(val);
    double c_val = cos(3.14159265358979323846);
    double sq = sqrt(16.0);
    printf("%s %2f\n", "sin(0.5):", s_val);
    printf("%s %2f\n", "cos(pi):", c_val);
    printf("%s %2f\n", "sqrt(16):", sq);
    printf("=== 究极测试运行结束 ===");
    printf("\n");
    return;
}


void pyc_test_ultimate_pdePycProc() {
    test_ultimate_features();
    return;
}

int main() {
    pyc_test_ultimate_pdePycProc();
    return 0;
}