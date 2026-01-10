#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

double calc_hypotenuse(int a, int b);

double calc_hypotenuse(int a, int b) {
    return sqrt((pow((double)(a), (double)(2)) + pow((double)(b), (double)(2))));
}


void pyc_test_inference_pdePycProc() {
    double res = calc_hypotenuse(3.0, 4.0);
    printf("%s %2f\n", "Hypotenuse:", res);
    return;
}

int main() {
    pyc_test_inference_pdePycProc();
    return 0;
}