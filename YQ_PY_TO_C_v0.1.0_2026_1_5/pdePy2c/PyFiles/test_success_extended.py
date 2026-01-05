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
