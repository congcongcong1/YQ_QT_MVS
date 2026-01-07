# -*- coding: utf-8 -*-
# test_success_extended.py
# 用于回归测试 pdePy2c (Python 子集 -> C) 的“常见语法覆盖”
#
# 覆盖点（当前转换器计划支持/已支持）：
# - int/float/bool/str 赋值与表达式
# - print(单参/多参)；print("text", int_expr) 合并输出
# - list[int] 字面量 -> C 数组 + <var>_len
# - 下标读写 arr[i]
# - len(arr)（在函数中替换为长度形参）
# - if / elif / else
# - for i in range(...)（含 step 为负数的常量）
# - while（不含 while-else）
# - break / continue
# - 函数定义/调用、return expr

print("test_success_extended.py")

def clamp(x, lo, hi):
    # if/elif/else + return expr
    if x < lo:
        return lo
    elif x > hi:
        return hi
    else:
        return x

def sum_array(arr):
    # 通过 len(arr) 触发：arr -> int* + 自动追加 n 形参，并把 len(arr) 替换为 n
    s = 0
    for i in range(len(arr)):
        s += arr[i]
    return s

def bubble_sort(arr, n):
    # 冒泡排序（优化版）
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
    return

# ---------- 基础表达式 ----------
a = 10
b = 20
result = (a + b) * 2
ratio = 1.5
mixed = result * ratio   # float 运算
ok = (result > 50) and (a < b)  # bool 运算

print("result", result)
print("mixed", mixed)
print("ok", ok)

# ---------- if / elif / else ----------
if result > 100:
    print("huge", result)
elif result > 50:
    print("large", result)
else:
    print("small", result)

# ---------- while + break/continue ----------
cnt = 0
acc = 0

while cnt < 10:
    cnt += 1
    if (cnt % 2) == 0:
        continue
    acc += cnt
    if acc > 20:
        break

print("acc", acc)

# ---------- for range step（含负步长常量）----------
rev_sum = 0
for k in range(5, 0, -1):
    rev_sum += k
print("rev_sum", rev_sum)

# ---------- 数组/len/下标 ----------
arr = [5, 1, 4, 2, 8]
n = 5

print("before sort:", arr)
bubble_sort(arr, n)
print("after sort:", arr)

total = sum_array(arr)
print("sum", total)

# ---------- 函数返回值 ----------
c = clamp(total, 0, 20)
print("clamp", c)

# ---------- 2D 数组 ----------
img = [[1, 2, 3], [4, 5, 6]]
print("img", img)
print("img[1][2]", img[1][2])



def sum2d(arr):
    s = 0
    for y in range(len(arr)):
        for x in range(len(arr[0])):
            s += arr[y][x]
    return s

print("sum2d", sum2d(img))

# ---------- struct ----------
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

p = Point(3, 4)
print("p.x", p.x)
p.x = p.x + 1
print("p.x2", p.x)

def sum_point(pt):
    return pt.x + pt.y

print("sum_point", sum_point(p))

# ---------- string (no concat) ----------
s1 = "hello"
s2 = "world"
print("s1", s1)
print("s2", s2)

# ---------- mcu ops (compile-time test; guarded for Python runtime) ----------
machine = 0

def test_mcu_ops():
    a1 = 0x0F
    b1 = 0xF0
    c1 = (a1 << 4) | b1
    d1 = c1 & 0xFF
    e1 = d1 ^ 0xAA
    f1 = ~e1
    print("mcu_a", a1)
    print("mcu_b", b1)
    print("mcu_c", c1)
    print("mcu_d", d1)
    print("mcu_e", e1)
    print("mcu_f", f1)
    if machine != 0:
        machine.mem32[0x40021018] |= (1 << 5)
        machine.mem32[0x40021018] &= ~0x20
    return f1

print("mcu_ops", test_mcu_ops())
