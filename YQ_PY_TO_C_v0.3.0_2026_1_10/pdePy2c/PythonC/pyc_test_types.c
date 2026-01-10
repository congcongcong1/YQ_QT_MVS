#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int64_t LARGE_VAL = 4294967296LL;
uint32_t ADDR = 0x40010800;
uint16_t HEX_LIST[] = {0x10, 0x20, 0x30};
int HEX_LIST_len = 3;

void test();

void test() {
  uint32_t x = 0xFFFFFFFFU;
  printf("%u\n", x);
  return;
}

void pyc_test_types_pdePycProc() { return; }

int main() {
  pyc_test_types_pdePycProc();
  return 0;
}