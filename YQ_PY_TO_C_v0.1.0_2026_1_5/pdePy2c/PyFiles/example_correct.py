# -*- coding: utf-8 -*-
"""
示例：正确的Python代码规范 (example_correct.py)

这个文件演示如何编写可以被pdePy2c转换器正确处理的Python代码。
遵循规范，避免常见错误。
"""

# ===== 全局变量声明 =====
# 规范：使用 g_ 前缀，避免与函数参数重名
# 注意：转换器会自动为全局数组生成_len变量，不要手动定义

g_array = [5, 2, 8, 1, 9, 3]

g_state = 0
g_count = 0

# ===== 工具函数 =====

def clamp(x, lo, hi):
    """限制值在范围内"""
    if x < lo:
        return lo
    elif x > hi:
        return hi
    else:
        return x

def sum_array(arr, n):
    """
    求和函数
    规范：参数完整 - arr是数组，n是长度参数
    """
    total = 0
    for i in range(n):
        total += arr[i]
    return total

def bubble_sort(arr, n):
    """
    冒泡排序
    规范：参数完整 - 每个数组参数都有对应的长度参数
    """
    for i in range(n - 1):
        swapped = 0
        for j in range(n - 1 - i):
            if arr[j] > arr[j + 1]:
                temp = arr[j]
                arr[j] = arr[j + 1]
                arr[j + 1] = temp
                swapped = 1
        if swapped == 0:
            return

def find_max(arr, n):
    """
    找最大值
    规范：参数完整，避免使用全局变量
    """
    if n <= 0:
        return 0
    max_val = arr[0]
    for i in range(1, n):
        if arr[i] > max_val:
            max_val = arr[i]
    return max_val

def copy_array(src, n_src, dst, n_dst):
    """
    复制数组
    规范：多个数组参数，每个都配长度参数
    """
    for i in range(n_src):
        if i < n_dst:
            dst[i] = src[i]

# ===== 主程序函数 =====

def process_global_data():
    """
    处理全局数据
    规范：调用函数时参数完整，数量和顺序与定义一致
    """
    print("Processing global data")
    
    # 调用sum_array：传全局数组和其长度
    total = sum_array(g_array, len(g_array))
    print("Sum:", total)
    
    # 调用bubble_sort：排序全局数组
    bubble_sort(g_array, len(g_array))
    print("Sorted")
    
    # 调用find_max：找最大值
    max_val = find_max(g_array, len(g_array))
    print("Max:", max_val)
    
    # 调用clamp：限制值
    clamped = clamp(max_val, 0, 10)
    print("Clamped:", clamped)

def test_local_arrays():
    """
    测试局部数组
    规范：局部数组用list字面量，调用时用len()获取长度
    """
    print("Testing local arrays")
    
    # 创建局部数组
    local_data = [10, 20, 30, 40, 50]
    
    # 调用sum_array：对局部数组，用len()
    local_sum = sum_array(local_data, len(local_data))
    print("Local sum:", local_sum)
    
    # 复制数组
    result = [0, 0, 0, 0, 0, 0]
    copy_array(local_data, len(local_data), result, len(result))
    print("Copy done")

def test_control_flow():
    """
    测试控制流
    规范：使用标准的if/elif/else、for、while、break、continue
    """
    print("Testing control flow")
    
    # if/elif/else
    x = 5
    if x < 0:
        print("Negative")
    elif x == 0:
        print("Zero")
    else:
        print("Positive")
    
    # for循环
    total = 0
    for i in range(5):
        total += i
    print("For sum:", total)
    
    # while循环
    counter = 0
    while counter < 3:
        counter += 1
        if counter == 2:
            continue
        print("Counter:", counter)
    
    # break
    for i in range(10):
        if i == 5:
            break
        print("Break test:", i)

def test_operations():
    """
    测试各种操作
    规范：使用基本的算术和逻辑运算
    """
    print("Testing operations")
    
    # 算术运算
    a = 10
    b = 3
    print("Add:", a + b)
    print("Sub:", a - b)
    print("Mul:", a * b)
    print("Div:", a / b)
    print("Mod:", a % b)
    
    # 比较
    if a > b:
        print("a > b")
    
    # 逻辑运算
    if a > 5 and b < 5:
        print("Both conditions true")
    
    if a > 20 or b < 5:
        print("At least one condition true")

# ===== 执行入口 =====

print("=== Python Code Standard Example ===")
print()

process_global_data()
print()

test_local_arrays()
print()

test_control_flow()
print()

test_operations()
print()

print("=== Done ===")
