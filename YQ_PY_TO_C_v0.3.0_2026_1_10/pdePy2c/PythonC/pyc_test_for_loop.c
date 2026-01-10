#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

void test_for_loop();

void test_for_loop() {
    printf("--- Test: for item in list ---");
    printf("\n");
    int items[] = { 10, 20, 30, 40, 50 };
    int items_len = 5;
    int __i_items;
    for (__i_items = 0; __i_items < items_len; __i_items++) {
        int item = items[__i_items];
        printf("%s %d\n", "Item:", item);
    }
    printf("\n--- Test: for val in double_list ---");
    printf("\n");
    double prices[] = { 1.99, 2.5, 3.75 };
    int prices_len = 3;
    double total = 0.0;
    int __i_prices;
    for (__i_prices = 0; __i_prices < prices_len; __i_prices++) {
        double p = prices[__i_prices];
        printf("%s %2f\n", "Price:", p);
        total += p;
    }
    printf("%s %2f\n", "Total:", total);
    return;
}


void pyc_test_for_loop_pdePycProc() {
    test_for_loop();
    return;
}

int main() {
    pyc_test_for_loop_pdePycProc();
    return 0;
}