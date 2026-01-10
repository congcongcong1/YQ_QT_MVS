#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int g_array[] = { 5, 2, 8, 1, 9, 3 };
int g_array_len = 6;
int g_state = 0;
int g_count = 0;

int clamp(int x, int lo, int hi);
int sum_array(int *arr, int n);
void bubble_sort(int *arr, int n);
int find_max(int *arr, int n);
void copy_array(int *src, int n_src, int *dst, int n_dst);
void process_global_data();
void test_local_arrays();
void test_control_flow();
void test_operations();

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

int sum_array(int *arr, int n) {
    int total = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        total += arr[i];
    }
    return total;
}

void bubble_sort(int *arr, int n) {
    int i;
    for (i = 0; i < (n - 1); i += 1) {
        int swapped = 0;
        int j;
        for (j = 0; j < ((n - 1) - i); j += 1) {
            if ((arr[j] > arr[(j + 1)])) {
                int temp = arr[j];
                arr[j] = arr[(j + 1)];
                arr[(j + 1)] = temp;
                swapped = 1;
            }
        }
        if ((swapped == 0)) {
            return;
        }
    }
    return;
}

int find_max(int *arr, int n) {
    if ((n <= 0)) {
        return 0;
    }
    int max_val = arr[0];
    int i;
    for (i = 1; i < n; i += 1) {
        if ((arr[i] > max_val)) {
            max_val = arr[i];
        }
    }
    return max_val;
}

void copy_array(int *src, int n_src, int *dst, int n_dst) {
    int i;
    for (i = 0; i < n_src; i += 1) {
        if ((i < n_dst)) {
            dst[i] = src[i];
        }
    }
    return;
}

void process_global_data() {
    printf("Processing global data");
    printf("\n");
    int total = sum_array(g_array, g_array_len);
    printf("%s %d\n", "Sum:", total);
    bubble_sort(g_array, g_array_len);
    printf("Sorted");
    printf("\n");
    int max_val = find_max(g_array, g_array_len);
    printf("%s %d\n", "Max:", max_val);
    int clamped = clamp(max_val, 0, 10);
    printf("%s %d\n", "Clamped:", clamped);
    return;
}

void test_local_arrays() {
    printf("Testing local arrays");
    printf("\n");
    int local_data[] = { 10, 20, 30, 40, 50 };
    int local_data_len = 5;
    int local_sum = sum_array(local_data, local_data_len);
    printf("%s %d\n", "Local sum:", local_sum);
    int result[] = { 0, 0, 0, 0, 0, 0 };
    int result_len = 6;
    copy_array(local_data, local_data_len, result, result_len);
    printf("Copy done");
    printf("\n");
    return;
}

void test_control_flow() {
    printf("Testing control flow");
    printf("\n");
    int x = 5;
    if ((x < 0)) {
        printf("Negative");
        printf("\n");
    } else {
        if ((x == 0)) {
            printf("Zero");
            printf("\n");
        } else {
            printf("Positive");
            printf("\n");
        }
    }
    int total = 0;
    int i;
    for (i = 0; i < 5; i += 1) {
        total += i;
    }
    printf("%s %d\n", "For sum:", total);
    int counter = 0;
    while ((counter < 3)) {
        counter += 1;
        if ((counter == 2)) {
            continue;
        }
        printf("%s %d\n", "Counter:", counter);
    }
    for (i = 0; i < 10; i += 1) {
        if ((i == 5)) {
            break;
        }
        printf("%s %d\n", "Break test:", i);
    }
    return;
}

void test_operations() {
    printf("Testing operations");
    printf("\n");
    int a = 10;
    int b = 3;
    printf("%s %d\n", "Add:", (a + b));
    printf("%s %d\n", "Sub:", (a - b));
    printf("%s %d\n", "Mul:", (a * b));
    printf("%s %2f\n", "Div:", ((double)(a) / (double)(b)));
    printf("%s %d\n", "Mod:", (a % b));
    if ((a > b)) {
        printf("a > b");
        printf("\n");
    }
    if (((a > 5) && (b < 5))) {
        printf("Both conditions true");
        printf("\n");
    }
    if (((a > 20) || (b < 5))) {
        printf("At least one condition true");
        printf("\n");
    }
    return;
}


void pyc_example_correct_pdePycProc() {
    printf("=== Python Code Standard Example ===");
    printf("\n");
    printf("\n");
    process_global_data();
    printf("\n");
    test_local_arrays();
    printf("\n");
    test_control_flow();
    printf("\n");
    test_operations();
    printf("\n");
    printf("=== Done ===");
    printf("\n");
    return;
}

int main() {
    pyc_example_correct_pdePycProc();
    return 0;
}