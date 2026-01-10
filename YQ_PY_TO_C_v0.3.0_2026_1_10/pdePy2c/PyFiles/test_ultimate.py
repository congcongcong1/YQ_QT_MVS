from typing import List
import math

# 1. 全局变量与复杂初始化
G_OFFSET = 100
G_MASK = 0xFFFFFFFF
G_SCALES = [1.0, 1.5, 2.25]
G_MATRIX = [
    [0.1, 0.2],
    [0.3, 0.4],
    [0.5, 0.6]
]

# 2. 结构体定义
class Vector3:
    def __init__(self, x, y, z):
        self.x = x
        self.y = y
        self.z = z

class PIDController:
    def __init__(self, kp, ki, kd):
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self.integral = 0.0
        self.last_error = 0.0

# 3. 辅助计算函数 (带类型注解)
def calculate_norm(v: Vector3) -> float:
    return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)

def process_signal(samples: List[float], gain: float) -> float:
    total = 0.0
    for i in range(len(samples)):
        samples[i] = samples[i] * gain
        total += samples[i]
    return total / len(samples)

def lcg_step(seed: int) -> int:
    # 验证 uint32 溢出回绕逻辑
    return (seed * 1664525 + 1013904223) % 4294967296

# 4. 核心功能测试集
def test_ultimate_features():
    print("=== 开始万无一失究极测试 ===")

    # (A) 基础算术与精度修复验证
    print("--- A. 算术与除法精度 ---")
    a = 10
    b = 3
    div_res = a / b      # 应为 double 3.333
    floor_res = a // b   # 应为 int 3
    print("Float Div (10/3):", div_res)
    print("Floor Div (10//3):", floor_res)
    
    # (B) 列表与二维数组推断
    print("--- B. 列表与 2D 数组 ---")
    data = [1.2, 3.4, 5.6]
    print("1D samples:", data)
    avg = process_signal(data, 2.0)
    print("Signal Mean after Gain:", avg)
    # print("Matrix Row 1:", G_MATRIX[1]) # Accessing a full row of a 2D array is not supported in C flattening
    print("Matrix [1][1]:", G_MATRIX[1][1])
    print("Matrix [2][1]:", G_MATRIX[2][1])
    
    # (C) 结构体复杂度
    print("--- C. 结构体与参数传递 ---")
    v = Vector3(1.0, 2.0, 3.0)
    norm = calculate_norm(v)
    print("Vector Norm:", norm)
    
    pid = PIDController(1.5, 0.1, 0.05)
    pid.integral += 1.2
    print("PID Kp:", pid.kp, "Integral:", pid.integral)

    # (D) 控制流与逻辑测试
    print("--- D. 控制流与布尔逻辑 ---")
    count = 0
    for i in range(10):
        if i % 2 == 0:
            continue
        if i > 7:
            break
        count += 1
    
    status = count == 4
    print("Loop Status (count=4?):", status)
    
    if "test" == "test":
        print("String equality works.")

    # (E) 位运算与大整数处理
    print("--- E. 位运算与大整数 ---")
    u_val = 2147483647
    u_next = lcg_step(u_val)
    print("LCG sequence next:", u_next)
    
    flags = 0xAA  # 10101010
    mask = 0x0F   # 00001111
    print("Bitwise AND:", flags & mask)
    print("Bitwise OR:", flags | mask)

    # (F) 数学函数与常数
    print("--- F. 数学库 ---")
    val = 0.5
    s_val = math.sin(val)
    c_val = math.cos(math.pi)
    sq = math.sqrt(16.0)
    print("sin(0.5):", s_val)
    print("cos(pi):", c_val)
    print("sqrt(16):", sq)

    print("=== 究极测试运行结束 ===")

test_ultimate_features()
