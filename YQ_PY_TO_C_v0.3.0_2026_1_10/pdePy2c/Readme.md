1. `pdePy2c_fixed.py`：稳定可用版本。 `pdePy2c.py`: 每周最后一天提交的代码
2. `pdePy2c_impl.py`：试验新功能的版本，不保证成功率。
   `pdePy2c.py`：每周最后一天提交的最新版本
3. 已实现功能请参考 `已实现功能.txt`。
   `测试报告.md`：测试PyFiles中一些函数的转译功能而设计的，并输出的报告
4. 运行方法：`python pdePy2c/pdePy2c.py`
   - 检测 `PyFiles` 中的所有 Python 文件，结果输出到 `result_1.txt`。
   - 为每个 `.py` 生成 C 文件，输出到 `PythonC` 目录，命名为 `pyc_<name>.c`。
5. 说明：
   - 请在 Python 文件开头定义 `byref/extern` 辅助函数和 `@extern` 装饰器（见 `PyFiles/LED.py`）。
   - 在 Python 文件顶部写注释形式的 `#include "xxx.h"`，翻译时会自动加入到生成的 C 文件头部。

## 如果要用取地址以及外部函数，需要定义以下辅助函数：
```py
# 1) 模拟 C 的取地址操作 (&var)
def byref(x):
    return x

# 2) 外部函数标记装饰器
def extern(func):
    return func
```
