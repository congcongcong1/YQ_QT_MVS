# -*- coding: utf-8 -*-
"""
边界条件与压力测试
测试目标：验证转译器在极端情况下的稳定性
"""

print("=== 边界条件测试开始 ===")

# ========== 1. 数组长度边界 ==========
print("--- 测试1: 数组长度边界 ---")

def test_array_boundaries():
    # 最小数组
    arr1 = [1]
    print("Single element:", arr1[0])
    
    # 较大数组
    arr_large = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19]
    n = len(arr_large)
    sum_val = 0
    for i in range(n):
        sum_val += arr_large[i]
    print("Large array sum:", sum_val)

test_array_boundaries()

# ========== 2. 嵌套深度测试 ==========
print("--- 测试2: 嵌套深度 ---")

def test_nested_loops():
    # 三层嵌套
    count = 0
    for i in range(3):
        for j in range(3):
            for k in range(3):
                count += 1
    print("Triple nested count:", count)

def test_nested_if():
    x = 5
    y = 10
    z = 15
    if x > 0:
        if y > 5:
            if z > 10:
                print("All conditions met")

test_nested_loops()
test_nested_if()

# ========== 3. 函数调用链 ==========
print("--- 测试3: 函数调用链 ---")

def func_a(x):
    return x + 1

def func_b(x):
    return func_a(x) * 2

def func_c(x):
    return func_b(x) - 3

def func_d(x):
    return func_c(x) / 2

def test_call_chain():
    result = func_d(10)
    print("Call chain result:", result)

test_call_chain()

# ========== 4. 复杂表达式 ==========
print("--- 测试4: 复杂表达式 ---")

def test_complex_expressions():
    a = 5
    b = 10
    c = 15
    
    # 多重运算
    result1 = (a + b) * c - (a * b) / c
    print("Complex expr 1:", result1)
    
    # 多重比较
    result2 = (a < b) and (b < c) and (a + b < c)
    print("Complex expr 2:", result2)
    
    # 位运算组合
    x = 0xFF
    y = 0x0F
    result3 = ((x & y) | (x ^ y)) << 2
    print("Complex expr 3:", result3)

test_complex_expressions()

# ========== 5. 数组操作边界 ==========
print("--- 测试5: 数组操作边界 ---")

def reverse_array(arr, n):
    i = 0
    j = n - 1
    while i < j:
        temp = arr[i]
        arr[i] = arr[j]
        arr[j] = temp
        i += 1
        j -= 1

def test_array_operations():
    data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    n = len(data)
    print("Before reverse:", data)
    reverse_array(data, n)
    print("After reverse:", data)

test_array_operations()

# ========== 6. 二维数组边界 ==========
print("--- 测试6: 二维数组边界 ---")

def test_2d_boundaries():
    # 1x1 最小二维数组
    tiny = [[42]]
    print("Tiny 2D:", tiny[0][0])
    
    # 不规则大小
    mat = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]]
    h = len(mat)
    w = len(mat[0])
    
    # 转置
    for y in range(h):
        for x in range(w):
            if x < h and y < w:
                temp = mat[y][x]

test_2d_boundaries()

# ========== 7. 控制流边界 ==========
print("--- 测试7: 控制流边界 ---")

def test_control_flow_boundaries():
    # range(0) - 空循环
    count = 0
    for i in range(0):
        count += 1
    print("Empty range count:", count)
    
    # range(1) - 单次循环
    for i in range(1):
        print("Single iteration:", i)
    
    # while 立即退出
    x = 0
    while x > 0:
        x -= 1
    print("While never entered:", x)
    
    # 立即 break
    for i in range(100):
        break
    print("Immediate break")

test_control_flow_boundaries()

# ========== 8. 数值边界 ==========
print("--- 测试8: 数值边界 ---")

def test_numeric_boundaries():
    # 零值
    zero_int = 0
    zero_float = 0.0
    print("Zero int:", zero_int)
    print("Zero float:", zero_float)
    
    # 负数
    neg_int = -100
    neg_float = -3.14
    print("Negative int:", neg_int)
    print("Negative float:", neg_float)
    
    # 大数
    big_int = 999999
    big_float = 123456.789
    print("Big int:", big_int)
    print("Big float:", big_float)

test_numeric_boundaries()

# ========== 9. 结构体边界 ==========
print("--- 测试9: 结构体边界 ---")

class MinimalStruct:
    def __init__(self, x):
        self.x = x

class LargeStruct:
    def __init__(self, a, b, c, d, e):
        self.a = a
        self.b = b
        self.c = c
        self.d = d
        self.e = e

def test_struct_boundaries():
    s1 = MinimalStruct(42)
    print("Minimal struct:", s1.x)
    
    s2 = LargeStruct(1, 2, 3, 4, 5)
    sum_val = s2.a + s2.b + s2.c + s2.d + s2.e
    print("Large struct sum:", sum_val)

test_struct_boundaries()

# ========== 10. 递归深度测试（有限深度）==========
print("--- 测试10: 有限递归 ---")

def countdown(n):
    if n <= 0:
        print("Done")
        return
    print("Count:", n)
    countdown(n - 1)

def test_limited_recursion():
    countdown(5)

test_limited_recursion()

# ========== 11. 混合类型运算 ==========
print("--- 测试11: 混合类型运算 ---")

def test_mixed_types():
    i = 10
    f = 3.5
    
    # int + float
    result1 = i + f
    print("int + float:", result1)
    
    # int * float
    result2 = i * f
    print("int * float:", result2)
    
    # int / int -> float
    result3 = i / 3
    print("int / int:", result3)

test_mixed_types()

# ========== 12. 字符串边界 ==========
print("--- 测试12: 字符串边界 ---")

def test_string_boundaries():
    # 空字符串
    empty = ""
    print("Empty string:", empty)
    
    # 单字符
    single = "a"
    print("Single char:", single)
    
    # 长字符串
    long_str = "This is a very long string for testing purposes with many characters"
    print("Long string:", long_str)
    
    # 特殊字符
    special = "Tab:\t Newline:\n Quote:\" Backslash:\\"
    print("Special chars:", special)

test_string_boundaries()

# ========== 13. 全局变量访问压力 ==========
print("--- 测试13: 全局变量访问 ---")

g_counter = 0
g_data = [10, 20, 30, 40, 50]

def increment_global():
    global g_counter
    g_counter += 1

def access_global_array():
    n = len(g_data)
    sum_val = 0
    for i in range(n):
        sum_val += g_data[i]
    return sum_val

def test_global_access():
    increment_global()
    increment_global()
    increment_global()
    print("Global counter:", g_counter)
    
    total = access_global_array()
    print("Global array sum:", total)

test_global_access()

# ========== 14. 参数传递压力 ==========
print("--- 测试14: 多参数函数 ---")

def many_params(a, b, c, d, e, f, g, h):
    return a + b + c + d + e + f + g + h

def test_many_params():
    result = many_params(1, 2, 3, 4, 5, 6, 7, 8)
    print("Many params sum:", result)

test_many_params()

# ========== 15. 位运算边界 ==========
print("--- 测试15: 位运算边界 ---")

def test_bitwise_boundaries():
    # 全0
    zero = 0x00
    print("All zeros:", zero)
    
    # 全1 (8位)
    all_ones = 0xFF
    print("All ones:", all_ones)
    
    # 移位边界
    val = 1
    shifted = val << 7
    print("Shift left 7:", shifted)
    
    shifted_back = shifted >> 7
    print("Shift right 7:", shifted_back)
    
    # 取反
    inverted = ~0x00
    print("Inverted zero:", inverted)

test_bitwise_boundaries()

print("=== 边界条件测试完成 ===")
