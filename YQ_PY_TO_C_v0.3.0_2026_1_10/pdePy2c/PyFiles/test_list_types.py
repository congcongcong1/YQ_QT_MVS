from typing import List

# 1. 自动推断：一维浮点数组
prices = [10.5, 20.0, 30.75]
print("First price:", prices[0])

# 2. 自动推断：二维浮点数组
matrix = [
    [1.1, 1.2],
    [2.1, 2.2]
]
print("Matrix[1][0]:", matrix[1][0])

# 3. 类型注解：带 List[float] 的函数
def sum_list(items: List[float]) -> float:
    s = 0.0
    for i in range(len(items)):
        s += items[i]
    return s

total = sum_list(prices)
print("Total:", total)

# 4. 通用打印模式测试 (Fallback)
print("Results:", prices, "Total:", total)
print("Matrix:", matrix)
