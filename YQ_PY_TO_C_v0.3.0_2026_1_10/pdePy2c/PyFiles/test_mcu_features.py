# -*- coding: utf-8 -*-
"""
单片机特性测试
测试目标：验证嵌入式开发相关的特殊功能
"""

print("=== 单片机特性测试开始 ===")

# ========== 1. 位操作测试 ==========
print("--- 测试1: 位操作 ---")

def test_bit_operations():
    # 设置位
    reg = 0x00
    reg = reg | (1 << 5)  # 设置第5位
    print("Set bit 5:", reg)
    
    # 清除位
    reg = reg & ~(1 << 5)  # 清除第5位
    print("Clear bit 5:", reg)
    
    # 切换位
    reg = 0x0F
    reg = reg ^ (1 << 2)  # 切换第2位
    print("Toggle bit 2:", reg)
    
    # 测试位
    val = 0x20
    bit5 = (val >> 5) & 1
    print("Test bit 5:", bit5)
    
    # 多位操作
    mask = 0x0F
    data = 0xAB
    masked = data & mask
    print("Masked data:", masked)

test_bit_operations()

# ========== 2. 寄存器操作模拟 ==========
print("--- 测试2: 寄存器操作 ---")

def test_register_operations():
    # 模拟 GPIO 寄存器操作
    GPIO_ODR = 0x0000
    
    # 设置多个引脚
    GPIO_ODR = GPIO_ODR | 0x0020  # PA5
    GPIO_ODR = GPIO_ODR | 0x0040  # PA6
    print("GPIO_ODR after set:", GPIO_ODR)
    
    # 清除引脚
    GPIO_ODR = GPIO_ODR & ~0x0020  # 清除 PA5
    print("GPIO_ODR after clear:", GPIO_ODR)
    
    # 读-改-写
    temp = GPIO_ODR
    temp = temp & ~0x00F0  # 清除高4位
    temp = temp | 0x0050   # 设置新值
    GPIO_ODR = temp
    print("GPIO_ODR after RMW:", GPIO_ODR)

test_register_operations()

# ========== 3. uint32_t 测试 ==========
print("--- 测试3: uint32_t ---")

def lcg_random(seed):
    # 线性同余生成器 (LCG)
    # 需要 uint32_t 溢出行为
    a = 1664525
    c = 1013904223
    m = 4294967296  # 2^32
    
    result = (seed * a + c) % m
    return result

def test_uint32():
    seed = 12345
    print("Initial seed:", seed)
    
    for i in range(5):
        seed = lcg_random(seed)
        print("Random:", seed)

test_uint32()

# ========== 4. uint16_t 测试 ==========
print("--- 测试4: uint16_t ---")

def test_uint16():
    # 使用 hex 字面量触发 uint16_t
    val1 = 0xFFFF
    val2 = 0x1234
    val3 = 0xABCD
    
    print("uint16 val1:", val1)
    print("uint16 val2:", val2)
    print("uint16 val3:", val3)
    
    # 16位运算
    result = (val2 & 0xFF00) >> 8
    print("High byte:", result)
    
    result2 = val2 & 0x00FF
    print("Low byte:", result2)

test_uint16()

# ========== 5. 状态机测试 ==========
print("--- 测试5: 状态机 ---")

STATE_IDLE = 0
STATE_RUNNING = 1
STATE_PAUSED = 2
STATE_STOPPED = 3

def test_state_machine():
    state = STATE_IDLE
    counter = 0
    
    for i in range(10):
        if state == STATE_IDLE:
            if i == 2:
                state = STATE_RUNNING
                print("State: RUNNING")
        elif state == STATE_RUNNING:
            counter += 1
            if i == 5:
                state = STATE_PAUSED
                print("State: PAUSED")
        elif state == STATE_PAUSED:
            if i == 7:
                state = STATE_RUNNING
                print("State: RUNNING again")
        
        if i == 9:
            state = STATE_STOPPED
            print("State: STOPPED")
    
    print("Final counter:", counter)

test_state_machine()

# ========== 6. 环形缓冲区 ==========
print("--- 测试6: 环形缓冲区 ---")

def test_ring_buffer():
    BUFFER_SIZE = 8
    buffer = [0, 0, 0, 0, 0, 0, 0, 0]
    head = 0
    tail = 0
    count = 0
    
    # 写入数据
    for i in range(5):
        buffer[head] = i + 10
        head = (head + 1) % BUFFER_SIZE
        count += 1
    
    print("Buffer after write:", buffer)
    print("Count:", count)
    
    # 读取数据
    for i in range(3):
        val = buffer[tail]
        print("Read:", val)
        tail = (tail + 1) % BUFFER_SIZE
        count -= 1
    
    print("Count after read:", count)

test_ring_buffer()

# ========== 7. 定时器计数 ==========
print("--- 测试7: 定时器计数 ---")

def test_timer_counter():
    MAX_COUNT = 1000
    prescaler = 8
    counter = 0
    ticks = 0
    
    for i in range(100):
        counter += 1
        if counter >= prescaler:
            counter = 0
            ticks += 1
            if ticks >= MAX_COUNT:
                ticks = 0
                print("Timer overflow")
    
    print("Final ticks:", ticks)

test_timer_counter()

# ========== 8. 中断标志处理 ==========
print("--- 测试8: 中断标志 ---")

def test_interrupt_flags():
    IRQ_FLAG_TIMER = 0x01
    IRQ_FLAG_UART = 0x02
    IRQ_FLAG_ADC = 0x04
    IRQ_FLAG_GPIO = 0x08
    
    irq_status = 0x00
    
    # 设置标志
    irq_status = irq_status | IRQ_FLAG_TIMER
    irq_status = irq_status | IRQ_FLAG_UART
    print("IRQ status:", irq_status)
    
    # 检查标志
    if (irq_status & IRQ_FLAG_TIMER) != 0:
        print("Timer IRQ pending")
        # 清除标志
        irq_status = irq_status & ~IRQ_FLAG_TIMER
    
    if (irq_status & IRQ_FLAG_UART) != 0:
        print("UART IRQ pending")
        irq_status = irq_status & ~IRQ_FLAG_UART
    
    print("IRQ status after clear:", irq_status)

test_interrupt_flags()

# ========== 9. ADC 数据处理 ==========
print("--- 测试9: ADC 数据处理 ---")

def test_adc_processing():
    # 模拟 ADC 读数
    adc_samples = [512, 520, 508, 515, 510, 518, 512, 514]
    n = len(adc_samples)
    
    # 求平均值
    sum_val = 0
    for i in range(n):
        sum_val += adc_samples[i]
    
    average = sum_val / n
    print("ADC average:", average)
    
    # 查找最大最小值
    min_val = adc_samples[0]
    max_val = adc_samples[0]
    
    for i in range(1, n):
        if adc_samples[i] < min_val:
            min_val = adc_samples[i]
        if adc_samples[i] > max_val:
            max_val = adc_samples[i]
    
    print("ADC min:", min_val)
    print("ADC max:", max_val)

test_adc_processing()

# ========== 10. PWM 占空比计算 ==========
print("--- 测试10: PWM 占空比 ---")

def test_pwm_duty():
    ARR = 1000  # Auto-reload value
    duty_percent = 75
    
    # 计算 CCR 值
    CCR = (ARR * duty_percent) / 100
    print("PWM CCR for 75%:", CCR)
    
    # 不同占空比
    duties = [0, 25, 50, 75, 100]
    n_duties = 5
    for i in range(n_duties):
        d = duties[i]
        ccr = (ARR * d) / 100
        print("Duty", d, "% CCR:", ccr)

test_pwm_duty()

# ========== 11. 数据打包/解包 ==========
print("--- 测试11: 数据打包 ---")

def test_data_packing():
    # 将两个字节打包成一个16位值
    high_byte = 0xAB
    low_byte = 0xCD
    
    packed = (high_byte << 8) | low_byte
    print("Packed value:", packed)
    
    # 解包
    extracted_high = (packed >> 8) & 0xFF
    extracted_low = packed & 0xFF
    
    print("Extracted high:", extracted_high)
    print("Extracted low:", extracted_low)
    
    # 4字节打包
    b0 = 0x12
    b1 = 0x34
    b2 = 0x56
    b3 = 0x78
    
    packed32 = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0
    print("Packed 32-bit:", packed32)

test_data_packing()

# ========== 12. 校验和计算 ==========
print("--- 测试12: 校验和 ---")

def calculate_checksum(data, n):
    checksum = 0
    for i in range(n):
        checksum = checksum + data[i]
    return checksum & 0xFF

def test_checksum():
    packet = [0x01, 0x02, 0x03, 0x04, 0x05]
    n = len(packet)
    
    cs = calculate_checksum(packet, n)
    print("Checksum:", cs)
    
    # 验证 - 创建包含校验和的数据包
    packet_with_cs = [0x01, 0x02, 0x03, 0x04, 0x05, 0x00]  # 先用0占位
    packet_with_cs[5] = cs  # 然后赋值
    total = calculate_checksum(packet_with_cs, len(packet_with_cs))
    print("Verify checksum:", total)

test_checksum()

# ========== 13. 滤波算法 ==========
print("--- 测试13: 简单滤波 ---")

def moving_average(data, n, window, result, result_len):
    # result 是输出数组，通过参数传入
    for i in range(n):
        sum_val = 0
        count = 0
        
        for j in range(window):
            idx = i - j
            if idx >= 0:
                sum_val += data[idx]
                count += 1
        
        if count > 0:
            result[i] = sum_val / count

def test_filtering():
    noisy_data = [10, 15, 12, 18, 14, 16, 13, 17, 15, 14]
    n = len(noisy_data)
    filtered = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    
    moving_average(noisy_data, n, 3, filtered, len(filtered))
    
    print("Original:", noisy_data)
    print("Filtered:", filtered)

test_filtering()

# ========== 14. 查找表 (LUT) ==========
print("--- 测试14: 查找表 ---")

def test_lookup_table():
    # 使用查找表获取正弦值（避免全局数组中的负数）
    sin_lut = [0, 707, 1000, 707, 0, 0, 0, 0]
    # 手动设置负数值
    sin_lut[5] = -707
    sin_lut[6] = -1000
    sin_lut[7] = -707
    
    n_lut = 8
    for i in range(n_lut):
        print("sin_lut[", i, "]:", sin_lut[i])

test_lookup_table()

# ========== 15. 二进制协议解析 ==========
print("--- 测试15: 协议解析 ---")

def parse_protocol(data, n):
    if n < 4:
        print("Packet too short")
        return
    
    header = data[0]
    length = data[1]
    cmd = data[2]
    
    print("Header:", header)
    print("Length:", length)
    print("Command:", cmd)
    
    # 解析数据
    if length > 0 and n >= 3 + length:
        for i in range(length):
            print("Data[", i, "]:", data[3 + i])

def test_protocol():
    packet = [0xAA, 0x03, 0x10, 0x01, 0x02, 0x03]
    parse_protocol(packet, len(packet))

test_protocol()

print("=== 单片机特性测试完成 ===")
