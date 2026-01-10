# -*- coding: utf-8 -*-
"""
全面测试 pdePy2c 转译器 - 简化版
避免复杂的属性访问和嵌套调用
"""

print("=== 综合测试开始 ===")

# ========== 1. 基础数据类型测试 ==========
print("--- 测试1: 基础数据类型 ---")

def test_basic_types():
    i1 = 42
    i2 = -10
    i3 = 0
    print("int:", i1, i2, i3)
    
    f1 = 3.14
    f2 = -2.5
    f3 = 0.0
    print("float:", f1, f2, f3)
    
    b1 = 1
    b2 = 0
    print("bool:", b1, b2)
    
    s1 = "hello"
    s2 = "world"
    s3 = "test \"quote\" and backslash \\"
    print("str:", s1)
    print("str:", s2)
    print("str:", s3)
    
    return 1

test_basic_types()

# ========== 2. 全局变量测试 ==========
print("--- 测试2: 全局变量 ---")

g_int = 100
g_float = 2.718
g_array = [1, 2, 3, 4, 5]
g_array2d = [[10, 20], [30, 40], [50, 60]]

def test_globals():
    print("g_int:", g_int)
    print("g_float:", g_float)
    print("g_array:", g_array)
    print("g_array2d:", g_array2d)
    return g_int

test_globals()

# ========== 3. 一维数组操作测试 ==========
print("--- 测试3: 一维数组 ---")

def test_1d_array_read(arr):
    n = len(arr)
    sum_val = 0
    for i in range(n):
        sum_val += arr[i]
    print("1D array sum:", sum_val)
    return sum_val

def test_1d_array_write(arr):
    n = len(arr)
    for i in range(n):
        arr[i] = i * 2
    print("1D array modified:", arr)

def test_1d_array_local():
    local = [7, 8, 9]
    print("Local array:", local)
    test_1d_array_read(local)
    test_1d_array_write(local)
    print("After write:", local)

test_1d_array_local()

# ========== 4. 二维数组操作测试 ==========
print("--- 测试4: 二维数组 ---")

def test_2d_array_read(arr, h, w):
    sum_val = 0
    for y in range(h):
        for x in range(w):
            sum_val += arr[y][x]
    print("2D array sum:", sum_val)
    return sum_val

def test_2d_array_write(arr, h, w):
    for y in range(h):
        for x in range(w):
            arr[y][x] = y * 10 + x

def test_2d_array_local():
    local2d = [[1, 2, 3], [4, 5, 6]]
    h = 2
    w = 3
    print("Local 2D array:", local2d)
    test_2d_array_read(local2d, h, w)
    test_2d_array_write(local2d, h, w)
    print("After write:", local2d)
    print("Element [1][2]:", local2d[1][2])

test_2d_array_local()

# ========== 5. 控制流测试 ==========
print("--- 测试5: 控制流 ---")

def test_if_elif_else(x):
    if x < 0:
        print("negative")
        return -1
    elif x == 0:
        print("zero")
        return 0
    else:
        print("positive")
        return 1

def test_for_range():
    sum1 = 0
    for i in range(5):
        sum1 += i
    print("range(5) sum:", sum1)
    
    sum2 = 0
    for i in range(2, 7):
        sum2 += i
    print("range(2,7) sum:", sum2)
    
    sum3 = 0
    for i in range(0, 10, 2):
        sum3 += i
    print("range(0,10,2) sum:", sum3)
    
    sum4 = 0
    for i in range(10, 0, -1):
        sum4 += i
    print("range(10,0,-1) sum:", sum4)

def test_while_loop():
    count = 0
    total = 0
    while count < 5:
        total += count
        count += 1
    print("while sum:", total)

def test_break_continue():
    sum_break = 0
    for i in range(100):
        if i >= 5:
            break
        sum_break += i
    print("break sum:", sum_break)
    
    sum_continue = 0
    for i in range(10):
        if i % 2 == 0:
            continue
        sum_continue += i
    print("continue sum:", sum_continue)

test_if_elif_else(5)
test_if_elif_else(0)
test_if_elif_else(-3)
test_for_range()
test_while_loop()
test_break_continue()

# ========== 6. 函数定义与调用测试 ==========
print("--- 测试6: 函数 ---")

def add(a, b):
    return a + b

def multiply(a, b):
    return a * b

def factorial(n):
    if n <= 1:
        return 1
    result = 1
    for i in range(2, n + 1):
        result *= i
    return result

def fibonacci(n):
    if n <= 1:
        return n
    a = 0
    b = 1
    for i in range(2, n + 1):
        c = a + b
        a = b
        b = c
    return b

print("add(3,4):", add(3, 4))
print("multiply(5,6):", multiply(5, 6))
print("factorial(5):", factorial(5))
print("fibonacci(10):", fibonacci(10))

# ========== 7. 结构体测试 ==========
print("--- 测试7: 结构体 ---")

class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

class Rectangle:
    def __init__(self, left, top, width, height):
        self.left = left
        self.top = top
        self.width = width
        self.height = height

def test_struct():
    p1 = Point(10, 20)
    px = p1.x
    py = p1.y
    print("Point x:", px)
    print("Point y:", py)
    
    p1.x = 30
    p1.y = 40
    px2 = p1.x
    py2 = p1.y
    print("Modified x:", px2)
    print("Modified y:", py2)
    
    r1 = Rectangle(0, 0, 100, 50)
    rw = r1.width
    rh = r1.height
    print("Rect width:", rw)
    print("Rect height:", rh)

def distance_squared(p1, p2):
    x1 = p1.x
    y1 = p1.y
    x2 = p2.x
    y2 = p2.y
    dx = x1 - x2
    dy = y1 - y2
    dist_sq = dx * dx + dy * dy
    return dist_sq

def test_struct_param():
    pa = Point(0, 0)
    pb = Point(3, 4)
    dist_sq = distance_squared(pa, pb)
    print("Distance squared:", dist_sq)

test_struct()
test_struct_param()

# ========== 8. 运算符测试 ==========
print("--- 测试8: 运算符 ---")

def test_arithmetic():
    a = 10
    b = 3
    print("a + b:", a + b)
    print("a - b:", a - b)
    print("a * b:", a * b)
    print("a / b:", a / b)
    print("a % b:", a % b)
    
    c = 5
    c += 2
    print("c += 2:", c)
    c -= 1
    print("c -= 1:", c)
    c *= 3
    print("c *= 3:", c)

def test_comparison():
    x = 5
    y = 10
    print("x < y:", x < y)
    print("x <= y:", x <= y)
    print("x > y:", x > y)
    print("x >= y:", x >= y)
    print("x == y:", x == y)
    print("x != y:", x != y)

def test_logical():
    a = 1
    b = 0
    print("a and b:", a and b)
    print("a or b:", a or b)
    print("not a:", not a)
    print("not b:", not b)

def test_bitwise():
    x = 0x0F
    y = 0xF0
    print("x & y:", x & y)
    print("x | y:", x | y)
    print("x ^ y:", x ^ y)
    print("~x:", ~x)
    print("x << 2:", x << 2)
    print("y >> 4:", y >> 4)

test_arithmetic()
test_comparison()
test_logical()
test_bitwise()

# ========== 9. 特殊功能测试 ==========
print("--- 测试9: 特殊功能 ---")

def test_len_function():
    arr1 = [1, 2, 3, 4, 5]
    n1 = len(arr1)
    print("len(arr1):", n1)

def test_print_variations():
    print()
    print(123)
    print(4.56)
    print("text")
    print("value:", 789)
    print("a", "b", "c")
    print("x:", 1, "y:", 2)

def test_uint_types():
    val32 = (123456789 * 2) % 4294967296
    print("uint32:", val32)
    
    val16 = 0xABCD
    print("uint16:", val16)

test_len_function()
test_print_variations()
test_uint_types()

# ========== 10. 综合算法测试 ==========
print("--- 测试10: 综合算法 ---")

def bubble_sort(arr, n):
    for i in range(n - 1):
        for j in range(n - 1 - i):
            if arr[j] > arr[j + 1]:
                temp = arr[j]
                arr[j] = arr[j + 1]
                arr[j + 1] = temp

def binary_search(arr, n, target):
    left = 0
    right = n - 1
    while left <= right:
        mid = (left + right) / 2
        mid_int = int(mid)
        if arr[mid_int] == target:
            return mid_int
        elif arr[mid_int] < target:
            left = mid_int + 1
        else:
            right = mid_int - 1
    return -1

def test_algorithms():
    data = [5, 2, 8, 1, 9, 3, 7, 4, 6]
    n = len(data)
    print("Before sort:", data)
    bubble_sort(data, n)
    print("After sort:", data)
    
    target = 7
    index = binary_search(data, n, target)
    print("Search 7:", index)

test_algorithms()

# ========== 11. 边界情况测试 ==========
print("--- 测试11: 边界情况 ---")

def test_edge_cases():
    empty = []
    n_empty = len(empty)
    print("empty len:", n_empty)
    
    single = [42]
    print("single:", single)
    
    for i in range(3):
        for j in range(3):
            if i == j:
                continue
            if i + j > 3:
                break
            print("i,j:", i, j)
    
    x = 5
    if x > 0:
        if x < 10:
            if x == 5:
                print("x is 5")

test_edge_cases()

# ========== 12. 类型转换测试 ==========
print("--- 测试12: 类型转换 ---")

def test_type_conversion():
    i = 10
    f = i / 3
    print("int to float:", f)
    
    f2 = 3.7
    i2 = int(f2)
    print("float to int:", i2)
    
    b = 1
    i3 = b + 5
    print("bool to int:", i3)

test_type_conversion()

print("=== 综合测试完成 ===")
