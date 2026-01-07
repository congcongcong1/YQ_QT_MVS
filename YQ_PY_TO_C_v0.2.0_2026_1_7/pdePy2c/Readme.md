1. `pdePy2c_fixed.py`：稳定可用版本。
2. `pdePy2c_impl.py`：试验新功能的版本，不保证成功率。
3. 已实现功能请参考 `已实现功能.txt`。
4. 运行方法：`python pdePy2c/pdePy2c_fixed.py`
   - 检测 `PyFiles` 中的所有 Python 文件，结果输出到 `result_1.txt`。
   - 为每个 `.py` 生成 C 文件，输出到 `PythonC` 目录，命名为 `pyc_<name>.c`。
5. `pdePy2c_impl.py` 目前支持：
   - 2D list(int) -> 平铺数组 + `arr_h/arr_w`，支持 `width(arr)` / `len(arr[0])`
   - struct 映射（class/namedtuple）
   - 位运算
   - `machine.mem32[addr]` 映射为 `MEM32(addr)`
   - `byref(x)` -> `&x`
   - `@extern` 跳过函数实现
   - 全大写常量赋值视为外部宏（`str + str` 为安全考虑已禁用）
   - 进制字面量（`0x...` / `0o...`）且值 <= `0xFFFF` 时，推断为 `uint16_t`
6. 说明：
   - 请在 Python 文件开头定义 `byref/extern` 辅助函数和 `@extern` 装饰器（见 `PyFiles/LED.py`）。
   - 在 Python 文件顶部写注释形式的 `#include "xxx.h"`，翻译时会自动加入到生成的 C 文件头部。

示例辅助函数：
```py
# 1) 模拟 C 的取地址操作 (&var)
def byref(x):
    return x

# 2) 外部函数标记装饰器
def extern(func):
    return func
```
