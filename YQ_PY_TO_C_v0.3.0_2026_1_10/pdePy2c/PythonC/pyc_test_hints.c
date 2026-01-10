#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int add(int a, int b);
double get_pi();
bool is_positive(int n);
double multiply(double a, double b);

int add(int a, int b) {
    return (a + b);
}

double get_pi() {
    return 3.14159;
}

bool is_positive(int n) {
    if ((n > 0)) {
        return 1;
    }
    return 0;
}

double multiply(double a, double b) {
    return (a * b);
}


void pyc_test_hints_pdePycProc() {
    printf("%d\n", add(1, 2));
    printf("%2f\n", get_pi());
    printf("%s\n", (is_positive(10) ? "True" : "False"));
    printf("%2f\n", multiply(2.5, 4.0));
    return;
}

int main() {
    pyc_test_hints_pdePycProc();
    return 0;
}