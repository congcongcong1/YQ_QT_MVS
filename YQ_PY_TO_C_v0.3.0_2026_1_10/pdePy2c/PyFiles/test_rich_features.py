# -*- coding: utf-8 -*-
# Rich test covering arrays, control flow, and calls with auto length args.

print("test_rich_features.py start")

g_seed = 12345
g_buf = [1, 2, 3, 4, 5]


def lcg_step(x):
    return (x * 1664525 + 1013904223) % 4294967296


def fill_with_seq(arr):
    n = len(arr)
    for i in range(n):
        arr[i] = (i * 3 + 1) % 7


def sum_even(arr):
    total = 0
    for i in range(len(arr)):
        if (arr[i] % 2) == 0:
            total = total + arr[i]
    return total


def reverse_in_place(arr):
    n = len(arr)
    i = 0
    j = n - 1
    while i < j:
        tmp = arr[i]
        arr[i] = arr[j]
        arr[j] = tmp
        i = i + 1
        j = j - 1


def compare_prefix(a, b):
    na = len(a)
    nb = len(b)
    limit = na
    if nb < limit:
        limit = nb
    ok = 1
    for i in range(limit):
        if a[i] != b[i]:
            ok = 0
    return ok


def test_ranges():
    total = 0
    for i in range(5):
        total = total + i
    for i in range(2, 7):
        total = total + i
    for i in range(10, 0, -2):
        total = total + i
    return total


def main():
    s = "string_with_quote: \"ok\" and backslash \\\\"
    print("String test:", s)

    local = [0, 0, 0, 0, 0]
    fill_with_seq(local)
    print("Filled:", local)

    even_sum = sum_even(local)
    print("Even sum:", even_sum)

    reverse_in_place(local)
    print("Reversed:", local)

    prefix_ok = compare_prefix(g_buf, local)
    print("Prefix ok:", prefix_ok)

    rng_total = test_ranges()
    print("Range total:", rng_total)

    x = g_seed
    for i in range(3):
        x = lcg_step(x)
    print("LCG last:", x)

    flag = (even_sum > 5) and (rng_total > 10)
    if not flag:
        print("Flag is false")
    else:
        print("Flag is true")


main()
