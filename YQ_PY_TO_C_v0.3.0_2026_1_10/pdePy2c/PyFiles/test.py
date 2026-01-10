# -*- coding: utf-8 -*-
# test.py: try to cover as many supported constructs as possible.

def byref(x):
    return x

def extern(func):
    return func

# ----- basics -----
a = 10
b = 0x0F
c = 0o77
d = 3.5
flag = True
msg = "hello"

arr = [1, 2, 3, 4]
mat = [[1, 2], [3, 4], [5, 6]]

print("msg", msg)
print("a", a, "b", b, "c", c, "d", d)
print("flag", flag)
print("arr", arr)
print("mat", mat)
print("mat[2][1]", mat[2][1])

# ----- control flow -----
def clamp(x, lo, hi):
    if x < lo:
        return lo
    elif x > hi:
        return hi
    else:
        return x

def loop_test(n):
    s = 0
    for i in range(0, n, 2):
        s += i
    for j in range(5, 0, -1):
        s += j
    k = 0
    while k < 10:
        k += 1
        if (k % 3) == 0:
            continue
        s += k
        if s > 50:
            break
    return s

print("loop_test", loop_test(12))

# ----- arrays and lengths -----
def sum_array(x):
    total = 0
    for i in range(len(x)):
        total += x[i]
    return total

def sum2d(x):
    total = 0
    for y in range(len(x)):
        for z in range(len(x[0])):
            total += x[y][z]
    return total

print("sum_array", sum_array(arr))
print("sum2d", sum2d(mat))

# ----- math and bit ops -----
def mix_math(x, y):
    add = x + y
    sub = x - y
    mul = x * y
    div = x / y
    flo = x // y
    mod = x % y
    return add + sub + mul + int(div) + flo + mod

def bit_mix(x, y):
    v = (x << 4) | y
    m = v & 0xFF
    n = m ^ 0xAA
    p = ~n
    return p

print("mix_math", mix_math(9, 4))
print("bit_mix", bit_mix(b, 0xF0))

# ----- bool ops -----
def bool_ops(x, y):
    t1 = (x > y) and (x != 0)
    t2 = (x < y) or (y == 0)
    t3 = not (x == y)
    if t1 and t2:
        return 1
    if t3:
        return 2
    return 0

print("bool_ops", bool_ops(a, 0))

# ----- struct mapping -----
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

def point_sum(p):
    return p.x + p.y

p = Point(3, 7)
p.x = p.x + 1
print("point_sum", point_sum(p))

# ----- uint32 wrap -----
def lcg_step(x):
    return (x * 1664525 + 1013904223) % 4294967296

print("lcg_step", lcg_step(123456))

# ----- local arrays -----
def local_arr():
    x = [5, 1, 4]
    return sum_array(x)

print("local_arr", local_arr())

# ----- mcu ops -----
machine = 0

def mem_ops():
    if machine != 0:
        machine.mem32[0x40021018] |= (1 << 5)
        machine.mem32[0x40021018] &= ~0x20
    return 0

print("mem_ops", mem_ops())
