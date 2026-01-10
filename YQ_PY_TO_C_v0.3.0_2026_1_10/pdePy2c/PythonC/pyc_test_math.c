#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

void test_math();

void test_math() {
    double x = 1.0;
    double s = sin(x);
    double c = cos(x);
    double t = tan(x);
    double sq = sqrt(2.0);
    double p = pow(2.0, 3.0);
    double pi = 3.14159265358979323846;
    printf("%s %2f\n", "sin(1.0) =", s);
    printf("%s %2f\n", "cos(1.0) =", c);
    printf("%s %2f\n", "sqrt(2.0) =", sq);
    printf("%s %2f\n", "pow(2,3) =", p);
    printf("%s %2f\n", "pi =", pi);
    int a = (-5);
    double fa = (-5.5);
    printf("%s %d\n", "abs(-5) =", abs(a));
    printf("%s %2f\n", "abs(-5.5) =", fabs(fa));
    return;
}


void pyc_test_math_pdePycProc() {
    test_math();
    return;
}

int main() {
    pyc_test_math_pdePycProc();
    return 0;
}