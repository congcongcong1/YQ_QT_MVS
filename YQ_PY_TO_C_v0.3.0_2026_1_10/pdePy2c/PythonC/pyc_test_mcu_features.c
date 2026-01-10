#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

int STATE_IDLE = 0;
int STATE_RUNNING = 1;
int STATE_PAUSED = 2;
int STATE_STOPPED = 3;

void test_bit_operations();
void test_register_operations();
int lcg_random(int seed);
void test_uint32();
void test_uint16();
void test_state_machine();
void test_ring_buffer();
void test_timer_counter();
void test_interrupt_flags();
void test_adc_processing();
void test_pwm_duty();
void test_data_packing();
int calculate_checksum(int *data, int n);
void test_checksum();
void moving_average(int *data, int n, int window, int *result, int result_len);
void test_filtering();
void test_lookup_table();
void parse_protocol(int *data, int n);
void test_protocol();

void test_bit_operations() {
    uint16_t reg = 0x00;
    reg = (reg | (1 << 5));
    printf("%s %u\n", "Set bit 5:", reg);
    reg = (reg & (~(1 << 5)));
    printf("%s %u\n", "Clear bit 5:", reg);
    reg = 0x0F;
    reg = (reg ^ (1 << 2));
    printf("%s %u\n", "Toggle bit 2:", reg);
    uint16_t val = 0x20;
    int bit5 = ((val >> 5) & 1);
    printf("%s %d\n", "Test bit 5:", bit5);
    uint16_t mask = 0x0F;
    uint16_t data = 0xAB;
    int masked = (data & mask);
    printf("%s %d\n", "Masked data:", masked);
    return;
}

void test_register_operations() {
    uint16_t GPIO_ODR = 0x0000;
    GPIO_ODR = (GPIO_ODR | 0x0020);
    GPIO_ODR = (GPIO_ODR | 0x0040);
    printf("%s %u\n", "GPIO_ODR after set:", GPIO_ODR);
    GPIO_ODR = (GPIO_ODR & (~0x0020));
    printf("%s %u\n", "GPIO_ODR after clear:", GPIO_ODR);
    uint16_t temp = GPIO_ODR;
    temp = (temp & (~0x00F0));
    temp = (temp | 0x0050);
    GPIO_ODR = temp;
    printf("%s %u\n", "GPIO_ODR after RMW:", GPIO_ODR);
    return;
}

int lcg_random(int seed) {
    int a = 1664525;
    int c = 1013904223;
    int64_t m = 4294967296LL;
    int result = (((seed * a) + c) % m);
    return result;
}

void test_uint32() {
    int seed = 12345;
    printf("%s %d\n", "Initial seed:", seed);
    int i;
    for (i = 0; i < 5; i += 1) {
        seed = lcg_random(seed);
        printf("%s %d\n", "Random:", seed);
    }
    return;
}

void test_uint16() {
    uint16_t val1 = 0xFFFF;
    uint16_t val2 = 0x1234;
    uint16_t val3 = 0xABCD;
    printf("%s %u\n", "uint16 val1:", val1);
    printf("%s %u\n", "uint16 val2:", val2);
    printf("%s %u\n", "uint16 val3:", val3);
    int result = ((val2 & 0xFF00) >> 8);
    printf("%s %d\n", "High byte:", result);
    int result2 = (val2 & 0x00FF);
    printf("%s %d\n", "Low byte:", result2);
    return;
}

void test_state_machine() {
    int state = STATE_IDLE;
    int counter = 0;
    int i;
    for (i = 0; i < 10; i += 1) {
        if ((state == STATE_IDLE)) {
            if ((i == 2)) {
                state = STATE_RUNNING;
                printf("State: RUNNING");
                printf("\n");
            }
        } else {
            if ((state == STATE_RUNNING)) {
                counter += 1;
                if ((i == 5)) {
                    state = STATE_PAUSED;
                    printf("State: PAUSED");
                    printf("\n");
                }
            } else {
                if ((state == STATE_PAUSED)) {
                    if ((i == 7)) {
                        state = STATE_RUNNING;
                        printf("State: RUNNING again");
                        printf("\n");
                    }
                }
            }
        }
        if ((i == 9)) {
            state = STATE_STOPPED;
            printf("State: STOPPED");
            printf("\n");
        }
    }
    printf("%s %d\n", "Final counter:", counter);
    return;
}

void test_ring_buffer() {
    int BUFFER_SIZE = 8;
    int buffer[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int buffer_len = 8;
    int head = 0;
    int tail = 0;
    int count = 0;
    int i;
    for (i = 0; i < 5; i += 1) {
        buffer[head] = (i + 10);
        head = ((head + 1) % BUFFER_SIZE);
        count += 1;
    }
    printf("%s", "Buffer after write: ");
    for (int __i_print = 0; __i_print < buffer_len; __i_print++) {
        printf("%d", buffer[__i_print]);
        if (__i_print < buffer_len - 1) printf(" ");
    }
    printf("\n");
    printf("%s %d\n", "Count:", count);
    for (i = 0; i < 3; i += 1) {
        int val = buffer[tail];
        printf("%s %d\n", "Read:", val);
        tail = ((tail + 1) % BUFFER_SIZE);
        count -= 1;
    }
    printf("%s %d\n", "Count after read:", count);
    return;
}

void test_timer_counter() {
    int MAX_COUNT = 1000;
    int prescaler = 8;
    int counter = 0;
    int ticks = 0;
    int i;
    for (i = 0; i < 100; i += 1) {
        counter += 1;
        if ((counter >= prescaler)) {
            counter = 0;
            ticks += 1;
            if ((ticks >= MAX_COUNT)) {
                ticks = 0;
                printf("Timer overflow");
                printf("\n");
            }
        }
    }
    printf("%s %d\n", "Final ticks:", ticks);
    return;
}

void test_interrupt_flags() {
    uint16_t IRQ_FLAG_TIMER = 0x01;
    uint16_t IRQ_FLAG_UART = 0x02;
    uint16_t IRQ_FLAG_ADC = 0x04;
    uint16_t IRQ_FLAG_GPIO = 0x08;
    uint16_t irq_status = 0x00;
    irq_status = (irq_status | IRQ_FLAG_TIMER);
    irq_status = (irq_status | IRQ_FLAG_UART);
    printf("%s %u\n", "IRQ status:", irq_status);
    if (((irq_status & IRQ_FLAG_TIMER) != 0)) {
        printf("Timer IRQ pending");
        printf("\n");
        irq_status = (irq_status & (~IRQ_FLAG_TIMER));
    }
    if (((irq_status & IRQ_FLAG_UART) != 0)) {
        printf("UART IRQ pending");
        printf("\n");
        irq_status = (irq_status & (~IRQ_FLAG_UART));
    }
    printf("%s %u\n", "IRQ status after clear:", irq_status);
    return;
}

void test_adc_processing() {
    int adc_samples[] = { 512, 520, 508, 515, 510, 518, 512, 514 };
    int adc_samples_len = 8;
    int n = adc_samples_len;
    int sum_val = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        sum_val += adc_samples[i];
    }
    double average = ((double)(sum_val) / (double)(n));
    printf("%s %2f\n", "ADC average:", average);
    int min_val = adc_samples[0];
    int max_val = adc_samples[0];
    for (i = 1; i < n; i += 1) {
        if ((adc_samples[i] < min_val)) {
            min_val = adc_samples[i];
        }
        if ((adc_samples[i] > max_val)) {
            max_val = adc_samples[i];
        }
    }
    printf("%s %d\n", "ADC min:", min_val);
    printf("%s %d\n", "ADC max:", max_val);
    return;
}

void test_pwm_duty() {
    int ARR = 1000;
    int duty_percent = 75;
    double CCR = ((double)((ARR * duty_percent)) / (double)(100));
    printf("%s %2f\n", "PWM CCR for 75%:", CCR);
    int duties[] = { 0, 25, 50, 75, 100 };
    int duties_len = 5;
    int n_duties = 5;
    int i;
    for (i = 0; i < n_duties; i += 1) {
        int d = duties[i];
        double ccr = ((double)((ARR * d)) / (double)(100));
        printf("Duty");
        printf(" ");
        printf("%d", d);
        printf(" ");
        printf("% CCR:");
        printf(" ");
        printf("%2f\n", ccr);
    }
    return;
}

void test_data_packing() {
    uint16_t high_byte = 0xAB;
    uint16_t low_byte = 0xCD;
    int packed = ((high_byte << 8) | low_byte);
    printf("%s %d\n", "Packed value:", packed);
    int extracted_high = ((packed >> 8) & 0xFF);
    int extracted_low = (packed & 0xFF);
    printf("%s %d\n", "Extracted high:", extracted_high);
    printf("%s %d\n", "Extracted low:", extracted_low);
    uint16_t b0 = 0x12;
    uint16_t b1 = 0x34;
    uint16_t b2 = 0x56;
    uint16_t b3 = 0x78;
    int packed32 = ((((b3 << 24) | (b2 << 16)) | (b1 << 8)) | b0);
    printf("%s %d\n", "Packed 32-bit:", packed32);
    return;
}

int calculate_checksum(int *data, int n) {
    int checksum = 0;
    int i;
    for (i = 0; i < n; i += 1) {
        checksum = (checksum + data[i]);
    }
    return (checksum & 0xFF);
}

void test_checksum() {
    uint16_t packet[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    int packet_len = 5;
    int n = packet_len;
    int cs = calculate_checksum(packet, n);
    printf("%s %d\n", "Checksum:", cs);
    uint16_t packet_with_cs[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x00 };
    int packet_with_cs_len = 6;
    packet_with_cs[5] = cs;
    int total = calculate_checksum(packet_with_cs, packet_with_cs_len);
    printf("%s %d\n", "Verify checksum:", total);
    return;
}

void moving_average(int *data, int n, int window, int *result, int result_len) {
    int i;
    for (i = 0; i < n; i += 1) {
        int sum_val = 0;
        int count = 0;
        int j;
        for (j = 0; j < window; j += 1) {
            int idx = (i - j);
            if ((idx >= 0)) {
                sum_val += data[idx];
                count += 1;
            }
        }
        if ((count > 0)) {
            result[i] = ((double)(sum_val) / (double)(count));
        }
    }
    return;
}

void test_filtering() {
    int noisy_data[] = { 10, 15, 12, 18, 14, 16, 13, 17, 15, 14 };
    int noisy_data_len = 10;
    int n = noisy_data_len;
    int filtered[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    int filtered_len = 10;
    moving_average(noisy_data, n, 3, filtered, filtered_len);
    printf("%s", "Original: ");
    for (int __i_print = 0; __i_print < noisy_data_len; __i_print++) {
        printf("%d", noisy_data[__i_print]);
        if (__i_print < noisy_data_len - 1) printf(" ");
    }
    printf("\n");
    printf("%s", "Filtered: ");
    for (int __i_print = 0; __i_print < filtered_len; __i_print++) {
        printf("%d", filtered[__i_print]);
        if (__i_print < filtered_len - 1) printf(" ");
    }
    printf("\n");
    return;
}

void test_lookup_table() {
    int sin_lut[] = { 0, 707, 1000, 707, 0, 0, 0, 0 };
    int sin_lut_len = 8;
    sin_lut[5] = (-707);
    sin_lut[6] = (-1000);
    sin_lut[7] = (-707);
    int n_lut = 8;
    int i;
    for (i = 0; i < n_lut; i += 1) {
        printf("sin_lut[");
        printf(" ");
        printf("%d", i);
        printf(" ");
        printf("]:");
        printf(" ");
        printf("%d\n", sin_lut[i]);
    }
    return;
}

void parse_protocol(int *data, int n) {
    if ((n < 4)) {
        printf("Packet too short");
        printf("\n");
        return;
    }
    int header = data[0];
    int length = data[1];
    int cmd = data[2];
    printf("%s %d\n", "Header:", header);
    printf("%s %d\n", "Length:", length);
    printf("%s %d\n", "Command:", cmd);
    if (((length > 0) && (n >= (3 + length)))) {
        int i;
        for (i = 0; i < length; i += 1) {
            printf("Data[");
            printf(" ");
            printf("%d", i);
            printf(" ");
            printf("]:");
            printf(" ");
            printf("%d\n", data[(3 + i)]);
        }
    }
    return;
}

void test_protocol() {
    uint16_t packet[] = { 0xAA, 0x03, 0x10, 0x01, 0x02, 0x03 };
    int packet_len = 6;
    parse_protocol(packet, packet_len);
    return;
}


void pyc_test_mcu_features_pdePycProc() {
    printf("=== 单片机特性测试开始 ===");
    printf("\n");
    printf("--- 测试1: 位操作 ---");
    printf("\n");
    test_bit_operations();
    printf("--- 测试2: 寄存器操作 ---");
    printf("\n");
    test_register_operations();
    printf("--- 测试3: uint32_t ---");
    printf("\n");
    test_uint32();
    printf("--- 测试4: uint16_t ---");
    printf("\n");
    test_uint16();
    printf("--- 测试5: 状态机 ---");
    printf("\n");
    test_state_machine();
    printf("--- 测试6: 环形缓冲区 ---");
    printf("\n");
    test_ring_buffer();
    printf("--- 测试7: 定时器计数 ---");
    printf("\n");
    test_timer_counter();
    printf("--- 测试8: 中断标志 ---");
    printf("\n");
    test_interrupt_flags();
    printf("--- 测试9: ADC 数据处理 ---");
    printf("\n");
    test_adc_processing();
    printf("--- 测试10: PWM 占空比 ---");
    printf("\n");
    test_pwm_duty();
    printf("--- 测试11: 数据打包 ---");
    printf("\n");
    test_data_packing();
    printf("--- 测试12: 校验和 ---");
    printf("\n");
    test_checksum();
    printf("--- 测试13: 简单滤波 ---");
    printf("\n");
    test_filtering();
    printf("--- 测试14: 查找表 ---");
    printf("\n");
    test_lookup_table();
    printf("--- 测试15: 协议解析 ---");
    printf("\n");
    test_protocol();
    printf("=== 单片机特性测试完成 ===");
    printf("\n");
    return;
}

int main() {
    pyc_test_mcu_features_pdePycProc();
    return 0;
}