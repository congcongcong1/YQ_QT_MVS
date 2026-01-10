# -*- coding: utf-8 -*-
"""
错误报告测试文件
目的：触发各种类型的 TranslateError，验证行号显示是否正确

每个错误都被注释掉了，取消注释某一个来测试对应的错误报告
"""

print("=== 错误报告测试 ===")

# ========== 测试 4: 比较运算错误（链式比较）==========
# 预期：应该显示第 41 行
# def test_compare_error():
#     x = 5
#     if 0 < x < 10:  # 第 41 行 - 应该报错：仅支持简单比较
#         print("in range")

# ========== 测试 5: 不支持的比较运算符 ==========
# 预期：应该显示第 47 行
# def test_unsupported_compare():
#     x = 5
#     if x is None:  # 第 47 行 - 应该报错：不支持该比较运算
#         print("is None")

# ========== 测试 6: 字符串拼接 ==========
# 预期：应该显示第 53 行
# def test_string_concat():
#     s1 = "hello"
#     s2 = "world"
#     s3 = s1 + s2  # 第 53 行 - 应该报错：string concat disabled
#     print(s3)




# ========== 测试 9: while-else 不支持 ==========
# 预期：应该显示第 73 行
# def test_while_else():
#     x = 0
#     while x < 5:  # 第 73 行
#         x += 1
#     else:  # 应该报错：暂不支持 while-else
#         print("done")

# ========== 测试 10: range 参数数量错误 ==========
# 预期：应该显示第 81 行
# def test_range_args_error():
#     # 这个错误比较难触发，因为 range 支持 1-3 个参数
#     pass

# ========== 测试 11: 全局变量未定义 ==========
# 预期：应该显示第 87 行
# def test_global_undefined():
#     global undefined_var  # 第 87 行 - 应该报错：global 变量未在顶层定义
#     undefined_var = 10

# ========== 测试 12: 不支持的增强赋值 ==========
# 预期：应该显示第 93 行
# def test_unsupported_augassign():
#     x = 5
#     x **= 2  # 第 93 行 - 应该报错：不支持该增强赋值运算
#     print(x)

# ========== 测试 13: 仅支持单目标赋值 ==========
# 预期：应该显示第 99 行
# def test_multi_target_assign():
#     x = y = 10  # 第 99 行 - 应该报错：仅支持单目标赋值
#     print(x, y)

# ========== 测试 14: 不支持复杂赋值 ==========
# 预期：应该显示第 105 行
# def test_complex_assign():
#     arr = [1, 2, 3]
#     arr[0], arr[1] = arr[1], arr[0]  # 第 105 行 - 应该报错：不支持复杂赋值
#     print(arr)



# ========== 测试 16: struct 字段不存在 ==========
# 预期：应该显示第 119 行
# class Rectangle:
#     def __init__(self, w, h):
#         self.w = w
#         self.h = h

# def test_struct_field_error():
#     r = Rectangle(10, 20)
#     x = r.x  # 第 119 行 - 应该报错：struct Rectangle 没有字段 x
#     print(x)

# ========== 测试 17: namedtuple 参数错误 ==========
# 预期：应该显示第 127 行
# from collections import namedtuple
# MyTuple = namedtuple("MyTuple")  # 第 127 行 - 应该报错：namedtuple requires (typename, fields)

# ========== 测试 18: 2D list 行长度不一致 ==========
# 预期：应该显示第 131 行
# arr_irregular = [[1, 2], [3, 4, 5]]  # 第 131 行 - 应该报错：2D list rows must have the same length

# ========== 测试 19: 顶层全局变量重复定义 ==========
# 预期：应该显示第 135 行
# g_var = 10
# g_var = 20  # 第 135 行 - 应该报错：顶层全局变量重复定义

# ========== 测试 20: len() 参数错误 ==========
# 预期：应该显示第 141 行
# def test_len_error():
#     x = 10
#     n = len(x)  # 第 141 行 - 应该报错：len 仅支持 len(name) 或 len(arr[0])
#     print(n)

print("=== 测试完成 ===")
print("请取消注释某个测试函数来验证错误报告")

