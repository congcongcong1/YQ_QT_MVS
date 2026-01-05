# -*- coding: utf-8 -*-
import ast
import sys
import argparse
import os
import py_compile
from typing import Dict, List, Optional, Tuple, Set

# pdePy2c_impl_fixed2.py
# Project-level notes:
# - This transpiler targets a restricted Python subset and emits C code.
# - Core features: list->array + length vars, print formatting, and basic control flow.
# - Type inference is lightweight and tuned for this subset (int/double/bool/uint32_t).
# - Keep changes minimal and avoid altering semantics of supported Python code.

BINOP_MAP = {
    ast.Add: "+",
    ast.Sub: "-",
    ast.Mult: "*",
    ast.Div: "/",
    ast.FloorDiv: "/",
    ast.Mod: "%",
}

CMPOP_MAP = {
    ast.Lt: "<",
    ast.LtE: "<=",
    ast.Gt: ">",
    ast.GtE: ">=",
    ast.Eq: "==",
    ast.NotEq: "!=",
}

UNARY_MAP = {
    ast.UAdd: "+",
    ast.USub: "-",
    ast.Not: "!",
}

class TranslateError(Exception):
    pass


def _force_write_text(path: str, text: str, encoding: str = "utf-8") -> None:
    """
    Overwrite a text file robustly.
    If the target exists but is read-only (common on Windows when files are produced by other tools),
    we try to chmod/remove then retry.
    """
    try:
        with open(path, "w", encoding=encoding) as fp:
            fp.write(text)
        return
    except PermissionError:
        try:
            # try make writable then retry
            import stat
            os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
        except Exception:
            pass
        try:
            os.remove(path)
        except Exception:
            pass
        with open(path, "w", encoding=encoding) as fp:
            fp.write(text)
class CCodeGenerator(ast.NodeVisitor):
    def __init__(self, filename: str):
        self.filename = filename
        self.code: List[str] = []
        self.indent_level = 0
        self.indent_with = "    "

        # 顶层全局变量（C 文件级别）
        # 说明：仅把“可静态初始化”的顶层赋值提升为全局：
        # - int/double/char* 常量
        # - int 列表字面量（-> C 数组 + <var>_len）
        # 其他复杂顶层语句仍放进 wrapper proc 中执行。
        self.global_vars: Set[str] = set()

        # 每个 C 作用域（函数体、wrapper proc）一套“已声明变量集合”
        self.vars_stack: List[Set[str]] = [set()]

        # 作用域栈：记录“已知数组长度变量名”
        self.len_stack: List[Dict[str, str]] = [dict()]

        # 作用域栈：记录变量的 C 类型（用于 printf/形参/推断）
        # 取值：'int' | 'double' | 'char *' | 'int_arr'
        self.type_stack: List[Dict[str, str]] = [dict()]
        
        # 参数重映射栈：原始参数名 -> C 参数名（用于解决全局变量遮蔽问题）
        self.param_rename_stack: List[Dict[str, str]] = [dict()]
        # 预声明类型栈：用于在首次赋值前决定更合适的变量类型
        self.predecl_type_stack: List[Dict[str, str]] = [dict()]
        # 显式 global 声明栈：用于区分局部/全局赋值语义
        self.explicit_global_stack: List[Set[str]] = [set()]
        # 参数名集合栈：用于识别函数形参（避免错误的局部数组返回）
        self.param_name_stack: List[Set[str]] = [set()]
        # 标记当前作用域是否为 wrapper proc
        self.in_wrapper_stack: List[bool] = [False]
        # Python 函数名 -> C 函数名映射（处理 main 冲突）
        self.func_name_alias: Dict[str, str] = {}

        # func -> [(param_name, len_param_name)]，用于调用处补长度参数
        self.func_array_params: Dict[str, List[Tuple[str, str]]] = {}
        # func -> {原始参数名 -> C参数名}，用于调用处翻译参数
        self.func_param_rename_map: Dict[str, Dict[str, str]] = {}
        # func -> [arg1, arg2, ...] (original python arg order)
        self.func_arg_names: Dict[str, List[str]] = {}
        self.func_ret_types: Dict[str, str] = {}

        self._top_level_stmts: List[ast.stmt] = []

    # ----- type helpers -----
    def set_type_name(self, var: str, type_name: str) -> None:
        """Record the inferred C type for a variable in the current scope.

        type_name values used in this transpiler:
        - 'int'
        - 'double'
        - 'char *'
        - 'bool'
        - 'uint32_t'
        - 'int_arr'  (int array / pointer)
        """
        if not var:
            return
        # Normalize a few common spellings
        if type_name == "char*":
            type_name = "char *"
        self.type_stack[-1][var] = type_name

    def get_type_name(self, var: str, default: str | None = None) -> str | None:
        """Look up variable type from innermost scope to outermost."""
        if not var:
            return default
        for scope in reversed(self.type_stack):
            if var in scope:
                return scope[var]
        return default

    # ----- global helpers -----
    def _is_static_global_assign(self, node: ast.Assign) -> bool:
        """Whether a top-level Assign can be lifted to C file-scope.

        Supported (static-init) forms:
        - name = <int|float|str> constant
        - name = [<int>, <int>, ...]
        """
        if len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            return False
        v = node.value
        if isinstance(v, ast.List):
            return all(isinstance(e, ast.Constant) and isinstance(e.value, int) for e in v.elts)
        if isinstance(v, ast.Constant) and isinstance(v.value, (int, float, str)):
            return True
        return False

    def _emit_global_assign(self, node: ast.Assign) -> None:
        """Emit a C global declaration for a supported top-level assign."""
        assert len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
        var = node.targets[0].id
        val = node.value

        if var in self.global_vars:
            # C 文件级别不允许重复定义；如果用户重复写同名全局，这里直接报错更清晰
            raise TranslateError(f"顶层全局变量 {var} 重复定义（C 不允许重复定义全局变量）")

        # list literal -> int arr[] + int arr_len
        if isinstance(val, ast.List):
            elems = [str(e.value) for e in val.elts]  # type: ignore[attr-defined]
            n = len(elems)
            init = "{ " + ", ".join(elems) + " }"
            self.emit(f"int {var}[] = {init};")
            len_var = f"{var}_len"
            self.emit(f"int {len_var} = {n};")
            # 记录到“全局集合”和“长度映射（基作用域）”
            self.global_vars.add(var)
            self.global_vars.add(len_var)
            self.vars_stack[0].add(var)
            self.vars_stack[0].add(len_var)
            self.len_stack[0][var] = len_var
            self.type_stack[0][var] = "int_arr"
            self.type_stack[0][len_var] = "int"
            return

        # scalar constants
        if isinstance(val, ast.Constant):
            if isinstance(val.value, float):
                c_type = "double"
            elif isinstance(val.value, str):
                c_type = "char *"
            else:
                c_type = "int"
            self.emit(f"{c_type} {var} = {self.visit(val)};")
            self.global_vars.add(var)
            self.vars_stack[0].add(var)
            self.type_stack[0][var] = c_type
            return

        # Shouldn't reach here due to _is_static_global_assign
        raise TranslateError("不支持的顶层全局赋值")

    # ----- scope helpers -----
    @property
    def vars(self) -> Set[str]:
        return self.vars_stack[-1]

    def push_scope(self):
        self.vars_stack.append(set())
        self.len_stack.append(dict(self.len_stack[-1]))
        self.type_stack.append(dict(self.type_stack[-1]))
        self.param_rename_stack.append(dict())
        self.predecl_type_stack.append(dict())
        self.explicit_global_stack.append(set())
        self.param_name_stack.append(set())
        self.in_wrapper_stack.append(False)

    def pop_scope(self):
        self.vars_stack.pop()
        self.len_stack.pop()
        self.type_stack.pop()
        self.param_rename_stack.pop()
        self.predecl_type_stack.pop()
        self.explicit_global_stack.pop()
        self.param_name_stack.pop()
        self.in_wrapper_stack.pop()

    def _merge_types(self, cur: str | None, new: str) -> str:
        if not cur:
            return new
        if cur == new:
            return cur
        if cur == "double" or new == "double":
            return "double"
        if cur == "uint32_t" or new == "uint32_t":
            return "uint32_t"
        return cur

    def set_len_name(self, var: str, len_var: str):
        self.len_stack[-1][var] = len_var

    def get_len_name(self, var: str) -> Optional[str]:
        return self.len_stack[-1].get(var)

    def fresh_var(self, base: str) -> str:
        """Generate a new variable name that doesn't collide in current scope."""
        name = base
        k = 0
        while name in self.vars:
            k += 1
            name = f"{base}{k}"
        return name
    
    def get_renamed_param(self, param_name: str) -> str:
        """获取参数的重映射名称，若无重映射则返回原名"""
        return self.param_rename_stack[-1].get(param_name, param_name)
    
    def _get_expr_type(self, node: ast.AST) -> str:
        """递归判断 Python 表达式在 C 中对应的类型。"""
        if isinstance(node, ast.Constant):
            if isinstance(node.value, float): 
                return "double"
            if isinstance(node.value, str): 
                return "char *"
            if isinstance(node.value, bool): 
                return "bool"
            return "int"
        
        if isinstance(node, ast.Name):
            # 从作用域栈中查找已知变量的类型
            t = self.get_type_name(node.id)
            return t if t else "int"

        if isinstance(node, ast.List):
            # 处理列表类型，可以返回 int* 类型（C 中通过指针表示数组）
            return "int *"  # 你可以根据具体的需求调整返回类型

        if isinstance(node, ast.BinOp):
            if isinstance(node.op, ast.Mod):
                if isinstance(node.right, ast.Constant) and node.right.value == 4294967296:
                    return "uint32_t"
            # 只要左右操作数有一个是 double，结果就是 double
            if isinstance(node.op, ast.Div):
                return "double"
            lt = self._get_expr_type(node.left)
            rt = self._get_expr_type(node.right)
            if lt == "double" or rt == "double":
                return "double"
            return "int"

        if isinstance(node, (ast.Compare, ast.BoolOp)):
            return "bool"

        if isinstance(node, ast.Call):
            # 如果是调用已知函数，查找其返回类型
            if isinstance(node.func, ast.Name):
                return self.func_ret_types.get(node.func.id, "int")

        if isinstance(node, ast.UnaryOp):
            # 处理单目操作符（如 -x 或 not x）
            operand_type = self._get_expr_type(node.operand)
            if operand_type == "double":
                return "double"
            return "int"

        return "int"
        
    def emit_print_call(self, node: ast.Call):
        args = list(node.args)

        if len(args) == 0:
            self.emit('printf("\\n");')
            return

        # 1. 针对 "mixed %d" 警告的快速路径修复 (len(args) == 2)
        if len(args) == 2 and isinstance(args[0], ast.Constant) and isinstance(args[0].value, str):
            prefix_c = self._escape_c_string(args[0].value)
            a1 = args[1]
            atype = self._get_expr_type(a1)

            # Case: print("...", arr) -> 保持你原有的逻辑，打印 [1 2 3]
            ln = self.get_len_name(a1.id) if isinstance(a1, ast.Name) else None
            if ln:
                self.emit(f'printf("{prefix_c} ");')
                idx = self.fresh_var("__i_print")
                self.emit(f'for (int {idx} = 0; {idx} < {ln}; {idx}++) {{')
                self.indent(); self.emit(f'printf("%d", {a1.id}[{idx}]);')
                self.emit(f'if ({idx} < {ln} - 1) printf(" ");'); self.dedent()
                self.emit('}'); self.emit('printf("\\n");')
                return

            # Case: 根据类型自动匹配格式化符
            fmt = "%d"
            val_str = self.visit(a1)
            if atype == "double": fmt = "%2f" # 解决 mixed 警告
            elif atype == "char *": fmt = "%s"
            elif atype == "bool":
                fmt = "%s"
                val_str = f"({val_str} ? \"True\" : \"False\")" # 打印 Python 风格布尔
            elif atype == "uint32_t":
                fmt = "%u"

            self.emit(f'printf("{prefix_c} {fmt}\\n", {val_str});')
            return

        # 2. 通用 Fallback 逻辑 (多参数)
        for idx_arg, a in enumerate(args):
            last = (idx_arg == len(args) - 1)
            if idx_arg > 0:
                self.emit('printf(" ");')
            atype = self._get_expr_type(a)

            # 字符串常量
            if isinstance(a, ast.Constant) and isinstance(a.value, str):
                escaped_value = self._escape_c_string(a.value)
                if last:
                    self.emit(f'printf("{escaped_value}\\n");')
                else:
                    self.emit(f'printf("{escaped_value}");')
                continue

            # 数组打印 (带方括号格式)
            ln = self.get_len_name(a.id) if isinstance(a, ast.Name) else None
            if ln:
                idx = self.fresh_var("__i_print")
                self.emit('printf("[");')
                self.emit(f'for (int {idx} = 0; {idx} < {ln}; {idx}++) {{')
                self.indent()
                self.emit(f'if ({idx} > 0) printf(", ");')
                self.emit(f'printf("%d", {a.id}[{idx}]);')
                self.dedent()
                self.emit('}')
                if last:
                    self.emit('printf("]\\n");')
                else:
                    self.emit('printf("]");')
                continue

            # 其他标量
            val_str = self.visit(a)
            if atype == "double":
                fmt = "%2f\\n" if last else "%2f"
                self.emit(f'printf("{fmt}", (double){val_str});')
            elif atype == "bool":
                fmt = "%s\\n" if last else "%s"
                self.emit(f'printf("{fmt}", ({val_str} ? "True" : "False"));')
            elif atype == "uint32_t":
                fmt = "%u\\n" if last else "%u"
                self.emit(f'printf("{fmt}", (uint32_t){val_str});')
            else:
                fmt = "%d\\n" if last else "%d"
                self.emit(f'printf("{fmt}", {val_str});')
    def emit(self, line: str):
        indent = self.indent_with * self.indent_level
        self.code.append(f"{indent}{line}")

    def _escape_c_string(self, s: str) -> str:
        return (s.replace("\\", "\\\\")
                 .replace('"', '\\"')
                 .replace("\n", "\\n")
                 .replace("\r", "\\r")
                 .replace("\t", "\\t"))

    def indent(self):
        self.indent_level += 1

    def dedent(self):
        self.indent_level -= 1

    # ----- entry -----
    def generate(self, tree: ast.AST) -> str:
        self.emit("#include <stdio.h>")
        self.emit("#include <stdint.h>")
        self.emit("#include <stdlib.h>")
        self.emit("#include <stdbool.h>") # 新增
        self.emit("#include <string.h>")  # 新增
        self.emit("#include <math.h>") # 新增
        self.emit("")

        self.visit(tree)

        base = os.path.splitext(os.path.basename(self.filename))[0]
        proc_name = f"pyc_{base}_pdePycProc"

        # wrapper proc
        self.emit("")
        self.emit(f"void {proc_name}() {{")
        self.indent()
        self.push_scope()
        self.in_wrapper_stack[-1] = True
        for stmt in self._top_level_stmts:
            self.visit(stmt)
        self.emit("return;")
        self.pop_scope()
        self.dedent()
        self.emit("}")

        # main
        self.emit("")
        self.emit("int main() {")
        self.indent()
        self.emit(f"{proc_name}();")
        self.emit("return 0;")
        self.dedent()
        self.emit("}")

        return "\n".join(self.code)

    # ----- analysis helpers -----
    def _analyze_func_array_params(self, node: ast.FunctionDef) -> List[Tuple[str, str]]:
        """
        识别“数组形参”以及它们对应的“长度形参名”。

        规则（尽量不误伤）：
        - 若形参在函数体内出现下标访问 arr[i] 或 len(arr)，则认为它是数组形参。
        - 若函数本身已经有一个看起来像长度的形参（优先：{arr}_len, n, len），则复用它，不再新增。
        - 否则为每个数组形参新增一个长度形参名（单数组默认 n；多数组默认 n_<arr>），并确保不与现有形参冲突。
        """
        arg_names = [a.arg for a in node.args.args]
        is_array = {name: False for name in arg_names}
        uses_len = {name: False for name in arg_names}

        for sub in ast.walk(node):
            if isinstance(sub, ast.Subscript) and isinstance(sub.value, ast.Name):
                if sub.value.id in is_array:
                    is_array[sub.value.id] = True
            if isinstance(sub, ast.Call) and isinstance(sub.func, ast.Name) and sub.func.id == "len":
                if sub.args and isinstance(sub.args[0], ast.Name) and sub.args[0].id in uses_len:
                    uses_len[sub.args[0].id] = True

        array_args = [a for a in arg_names if is_array[a] or uses_len[a]]
        arr_params: List[Tuple[str, str]] = []

        def pick_existing_len_name(arr_name: str) -> str | None:
            # 优先复用更明确的命名
            for cand in (f"n_{arr_name}", f"{arr_name}_len", "n", "len"):
                if cand in arg_names and cand != arr_name:
                    return cand
            return None

        def make_unique(name: str) -> str:
            if name not in arg_names:
                return name
            k = 1
            while f"{name}_{k}" in arg_names:
                k += 1
            return f"{name}_{k}"

        for a in array_args:
            existing = pick_existing_len_name(a)
            if existing:
                # 如果已经存在长度参数（如 n_box），直接建立映射，不生f成新名字
                arr_params.append((a, existing))
            else:
                # 只有完全找不到相关参数时，才生成新的
                base = "n" if len(array_args) == 1 else f"n_{a}"
                # 再次确认生成的 base 不在参数表里
                if base in arg_names:
                    # 如果 n 在里面但不是给这个数组用的，才通过 make_unique 找别的
                    arr_params.append((a, base)) 
                else:
                    arr_params.append((a, base))

        return arr_params

    # ----- visitors -----
    def _infer_return_type(self, fn: ast.FunctionDef) -> str:
        """Tiny return-type inference for our Python subset.

        - If there is no `return <expr>`: void
        - Else: int (extend later)
        """
        for n in ast.walk(fn):
            if isinstance(n, ast.Return) and n.value is not None:
                return self._get_expr_type(n.value)
        return "void"


    def _analyze_all_func_array_params(self, mod: ast.Module) -> None:
        """Compute func_array_params for all functions with propagation.

        Rule:
        - A parameter is an array if it is indexed (p[i]) or used in len(p).
        - Propagate: if a function passes one of its parameters into another function's
          array-parameter position, that parameter is also an array.
        """
        func_defs: Dict[str, ast.FunctionDef] = {}
        func_args: Dict[str, List[str]] = {}
        for s in mod.body:
            if isinstance(s, ast.FunctionDef):
                func_defs[s.name] = s
                func_args[s.name] = [a.arg for a in s.args.args]
        self.func_arg_names = func_args

        # seed from local analysis
        arr_map: Dict[str, List[Tuple[str, str]]] = {}
        for name, fn in func_defs.items():
            arr_map[name] = self._analyze_func_array_params(fn)

        # helper: get position of param in signature
        def param_index(fn_name: str, param: str) -> int | None:
            try:
                return func_args[fn_name].index(param)
            except ValueError:
                return None

        changed = True
        while changed:
            changed = False
            for caller_name, fn in func_defs.items():
                caller_params = func_args[caller_name]
                # current known array params set for caller
                known_arr = {p for p, _ in arr_map.get(caller_name, [])}
                for sub in ast.walk(fn):
                    if isinstance(sub, ast.Call) and isinstance(sub.func, ast.Name):
                        callee = sub.func.id
                        if callee not in arr_map or not arr_map[callee]:
                            continue
                        # for each array param expected by callee, look at actual arg
                        for callee_arr_param, _callee_len in arr_map[callee]:
                            idx = param_index(callee, callee_arr_param)
                            if idx is None or idx >= len(sub.args):
                                continue
                            actual = sub.args[idx]
                            if isinstance(actual, ast.Name):
                                an = actual.id
                                if an in caller_params and an not in known_arr:
                                    # mark as array in caller
                                    # choose / create length param name for caller
                                    # prefer existing {an}_len or 'n' if present
                                    len_name = None
                                    for cand in (f"{an}_len", "n", "len"):
                                        if cand in caller_params and cand != an:
                                            len_name = cand
                                            break
                                    if len_name is None:
                                        len_name = f"{an}_len"
                                    arr_map.setdefault(caller_name, []).append((an, len_name))
                                    known_arr.add(an)
                                    changed = True

        self.func_array_params = arr_map

    def visit_Module(self, node: ast.Module):
        # Pre-analyze functions for array parameters (with propagation)
        self._analyze_all_func_array_params(node)
        # 1) 先把“可静态初始化”的顶层赋值提升为 C 全局变量
        #    这样能支持类似：state_box = [246813579, 0, 0] 这样的“全局数组”。
        lifted: Set[int] = set()
        for i, s in enumerate(node.body):
            if isinstance(s, ast.Assign) and self._is_static_global_assign(s):
                self._emit_global_assign(s)
                lifted.add(i)

        if lifted:
            self.emit("")

        # 2) 再输出所有顶层函数（C 不允许函数嵌套）
        funcs = [s for s in node.body if isinstance(s, ast.FunctionDef)]
        # 预计算返回类型与 main 重命名映射
        for f in funcs:
            self.func_ret_types[f.name] = self._infer_return_type(f)
            if f.name == "main":
                self.func_name_alias["main"] = "user_main"

        # 先输出函数原型，避免 C 的前置声明缺失
        for f in funcs:
            arr_params = self.func_array_params.get(f.name) or self._analyze_func_array_params(f)
            self.func_array_params[f.name] = arr_params
            parts = []
            rendered_names = set()
            param_rename_map = {}

            for arg in f.args.args:
                param_name = arg.arg
                c_param_name = param_name
                if param_name in self.global_vars:
                    c_param_name = f"_arg_{param_name}"
                    param_rename_map[param_name] = c_param_name
                is_arr = any(p == arg.arg for p, _ in arr_params)
                if is_arr:
                    parts.append(f"int *{c_param_name}")
                else:
                    parts.append(f"int {c_param_name}")
                rendered_names.add(c_param_name)

            for _, ln in arr_params:
                c_len_name = ln
                if ln in self.global_vars:
                    c_len_name = f"_arg_{ln}"
                    param_rename_map[ln] = c_len_name
                if c_len_name not in rendered_names:
                    parts.append(f"int {c_len_name}")
                    rendered_names.add(c_len_name)

            ret_type = self.func_ret_types.get(f.name, "void")
            c_func_name = self.func_name_alias.get(f.name, f.name)
            self.emit(f"{ret_type} {c_func_name}({', '.join(parts)});")

        if funcs:
            self.emit("")
        for f in funcs:
            self.visit(f)

        # 3) 其余顶层语句放到 wrapper proc 里执行
        self._top_level_stmts = [
            s for i, s in enumerate(node.body)
            if i not in lifted and not isinstance(s, ast.FunctionDef)
        ]

    def visit_FunctionDef(self, node: ast.FunctionDef):
        arr_params = self.func_array_params.get(node.name) or self._analyze_func_array_params(node)
        self.func_array_params[node.name] = arr_params

        parts = []
        # 核心逻辑：记录已经渲染到 C 参数表里的变量名
        rendered_names = set()
        
        # 构建参数重映射表：若参数与全局变量同名，则添加前缀 "_arg_"
        param_rename_map = {}

        # 1. 首先遍历 Python 源码中原本就有的参数
        for arg in node.args.args:
            param_name = arg.arg
            c_param_name = param_name
            
            # 如果与全局变量同名，重命名为 _arg_<name> 以避免 -Wshadow 警告
            if param_name in self.global_vars:
                c_param_name = f"_arg_{param_name}"
                param_rename_map[param_name] = c_param_name
            
            is_arr = any(p == arg.arg for p, _ in arr_params)
            if is_arr:
                parts.append(f"int *{c_param_name}")
            else:
                parts.append(f"int {c_param_name}")
            rendered_names.add(c_param_name)

        # 2. 遍历分析出的数组映射，只有还没渲染过的参数名才添加
        for orig_arr, ln in arr_params:
            # 处理长度参数名的重命名
            c_len_name = ln
            if ln in self.global_vars:
                c_len_name = f"_arg_{ln}"
                param_rename_map[ln] = c_len_name
            
            if c_len_name not in rendered_names:
                parts.append(f"int {c_len_name}")
                rendered_names.add(c_len_name)

        ret_type = self.func_ret_types.get(node.name) or self._infer_return_type(node)
        self.func_ret_types[node.name] = ret_type

        # 记录这个函数的参数重映射，供调用处查询
        self.func_param_rename_map[node.name] = param_rename_map.copy()

        c_func_name = self.func_name_alias.get(node.name, node.name)
        if node.name == "main":
            c_func_name = "user_main"
            self.func_name_alias["main"] = c_func_name

        self.emit(f"{ret_type} {c_func_name}({', '.join(parts)}) {{")
        self.indent()

        # 函数作用域
        self.push_scope()
        
        # 建立参数重映射，并存储在当前作用域中
        self.param_rename_stack[-1] = param_rename_map.copy()

        # 记录显式 global 声明
        explicit_globals: Set[str] = set()
        for s in node.body:
            if isinstance(s, ast.Global):
                explicit_globals.update(s.names)
        self.explicit_global_stack[-1] = explicit_globals.copy()

        # record parameter types in this scope
        # 使用重命名后的参数名
        for arg in node.args.args:
            c_name = param_rename_map.get(arg.arg, arg.arg)
            if any(p == arg.arg for p, _ in arr_params):
                self.set_type_name(c_name, "int_arr")
            else:
                self.set_type_name(c_name, "int")

        # 参数视为已声明
        for arg in node.args.args:
            c_name = param_rename_map.get(arg.arg, arg.arg)
            self.vars.add(c_name)
            self.param_name_stack[-1].add(c_name)
        for _, ln in arr_params:
            c_len_name = param_rename_map.get(ln, ln)
            self.vars.add(c_len_name)

        # len(arr) 在函数内替换为 n / n_<arg>
        # 关键：使用原始参数名作为键（因为Python代码中就是这样引用的）
        # 但值应该是C中实际的长度变量名（可能已重映射）
        for p, ln in arr_params:
            c_len_name = param_rename_map.get(ln, ln)
            # 注：这里保持 p（原始参数名）作为键，这样 len(arr) 中的 arr 可以正确查询
            self.set_len_name(p, c_len_name)

        # 预扫描：如果同一变量在函数内被赋值为更高精度/更安全的类型，提前确定声明类型
        predecl: Dict[str, str] = {}
        for sub in ast.walk(node):
            if isinstance(sub, ast.Assign):
                if len(sub.targets) != 1 or not isinstance(sub.targets[0], ast.Name):
                    continue
                tname = sub.targets[0].id
                if tname in param_rename_map:
                    tname = param_rename_map[tname]
                rhs_type = self._get_expr_type(sub.value)
                predecl[tname] = self._merge_types(predecl.get(tname), rhs_type)
        self.predecl_type_stack[-1] = predecl

        # 若函数内对同名全局变量赋值且未声明 global，则应在 C 中创建局部遮蔽变量
        local_shadow_globals: Set[str] = set()
        for sub in ast.walk(node):
            if isinstance(sub, ast.Assign):
                if len(sub.targets) != 1 or not isinstance(sub.targets[0], ast.Name):
                    continue
                tname = sub.targets[0].id
                if tname in self.global_vars and tname not in explicit_globals:
                    local_shadow_globals.add(tname)
            if isinstance(sub, ast.AugAssign) and isinstance(sub.target, ast.Name):
                tname = sub.target.id
                if tname in self.global_vars and tname not in explicit_globals:
                    local_shadow_globals.add(tname)
            if isinstance(sub, ast.For) and isinstance(sub.target, ast.Name):
                tname = sub.target.id
                if tname in self.global_vars and tname not in explicit_globals:
                    local_shadow_globals.add(tname)

        for name in sorted(local_shadow_globals):
            if name not in self.vars:
                c_type = predecl.get(name, "int")
                decl_type = "int *" if c_type == "int_arr" else c_type
                self.emit(f"{decl_type} {name};")
                self.vars.add(name)
                self.set_type_name(name, c_type)

        for stmt in node.body:
            self.visit(stmt)

        # 若函数体末尾已显式 return，则不再追加一个多余的 return;
        if not node.body or not isinstance(node.body[-1], ast.Return):
            if ret_type == "void":
                self.emit("return;")
            else:
                self.emit("return 0;")
        self.pop_scope()

        self.dedent()
        self.emit("}")
        self.emit("")

    def visit_Return(self, node: ast.Return):
        if node.value is None:
            self.emit("return;")
        else:
            if isinstance(node.value, ast.Name):
                name = node.value.id
                c_name = self.get_renamed_param(name)
                t = self.get_type_name(c_name)
                if t == "int_arr":
                    if c_name not in self.param_name_stack[-1]:
                        if name in self.global_vars and c_name not in self.vars:
                            pass
                        else:
                            raise TranslateError(
                                f"不支持返回局部数组 {name}（C 中局部数组不可安全返回）"
                            )
            self.emit(f"return {self.visit(node.value)};")

    def visit_Expr(self, node: ast.Expr):
        if isinstance(node.value, ast.Constant) and isinstance(node.value.value, str):
            return
        # Special-case: print(...) needs to emit multiple C statements
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name) and node.value.func.id == "print":
            self.emit_print_call(node.value)
            return
        self.emit(self.visit(node.value) + ";")

    def visit_Global(self, node: ast.Global):
        # EN: global declarations are handled in visit_FunctionDef; CN: global 在函数入口已处理.
        return

    def visit_Attribute(self, node: ast.Attribute) -> str:
        # 示例：如果遇到 list.append，可以报错提示 C 不支持对象方法
        # 或者如果是 math.pi 这种常量，可以进行转换
        raise TranslateError(f"C 转换器不支持属性访问/对象方法: {node.attr}")

    def visit_Assign(self, node: ast.Assign):
        if len(node.targets) != 1:
            raise TranslateError("仅支持单目标赋值")
        
        target = node.targets[0]

        # 处理索引赋值（类似 a[i] = value）
        if isinstance(target, ast.Subscript):
            self.emit(f"{self.visit(target)} = {self.visit(node.value)};")
            return

        # 处理普通变量赋值
        if not isinstance(target, ast.Name):
            raise TranslateError("不支持复杂赋值")

        var = target.id
        val_node = node.value

        # 处理列表字面量赋值（例如 arr = [1, 2, 3]）
        if isinstance(val_node, ast.List):
            if var in self.global_vars and var not in self.vars:
                raise TranslateError(
                    f"全局数组 {var} 通过列表字面量初始化后，暂不支持在函数/局部作用域重新用 list 整体赋值（C 数组不可整体赋值）"
                )
            
            elems: List[str] = []
            for e in val_node.elts:
                if not isinstance(e, ast.Constant) or not isinstance(e.value, int):
                    raise TranslateError("当前仅支持 int 列表字面量")
                elems.append(str(e.value))
            
            n = len(elems)
            init = "{ " + ", ".join(elems) + " }"

            # 检查是否已经声明过变量（避免重复声明）
            if var in self.vars:
                raise TranslateError(f"变量 {var} 已声明，暂不支持再次整体赋 list（C 数组不可整体赋值）")

            # 声明数组变量和初始化
            self.emit(f"int {var}[] = {init};")
            self.vars.add(var)

            # 声明数组长度变量
            len_var = f"{var}_len"
            if len_var not in self.vars:
                self.emit(f"int {len_var} = {n};")
                self.vars.add(len_var)
            else:
                self.emit(f"{len_var} = {n};")

            self.set_len_name(var, len_var)
            self.set_type_name(var, "int_arr")
            self.set_type_name(len_var, "int")
            return

        rhs = self.visit(val_node)

        explicit_globals = self.explicit_global_stack[-1]
        # 处理全局变量赋值/遮蔽语义
        if var in self.global_vars and var not in self.vars:
            if self.in_wrapper_stack[-1]:
                if rhs == var:
                    return
                self.emit(f"{var} = {rhs};")
                return
            if var in explicit_globals:
                if rhs == var:
                    return
                self.emit(f"{var} = {rhs};")
                return
            # 未声明 global 的情况下，对同名全局变量赋值应创建局部变量
            c_type = self._get_expr_type(val_node)
            predecl_type = self.predecl_type_stack[-1].get(var)
            if predecl_type:
                c_type = self._merge_types(c_type, predecl_type)
            decl_type = "int *" if c_type == "int_arr" else c_type
            self.emit(f"{decl_type} {var} = {rhs};")
            self.vars.add(var)
            self.set_type_name(var, c_type)
            return
        if var in explicit_globals and var not in self.global_vars:
            raise TranslateError(f"global 变量 {var} 未在顶层定义")

        # 处理局部变量赋值
        if var in self.vars:
            if rhs == var:
                return
            self.emit(f"{var} = {rhs};")
        else:
            # --- 关键修改：使用类型推导函数 ---
            c_type = self._get_expr_type(val_node)
            predecl_type = self.predecl_type_stack[-1].get(var)
            if predecl_type:
                c_type = self._merge_types(c_type, predecl_type)

            # 如果推导出的类型是 int_arr（数组），在非 list 字面量赋值时应视为 int*
            decl_type = "int *" if c_type == "int_arr" else c_type

            # 如果变量已经在函数参数中声明，就不再重复声明
            if var not in self.vars:
                self.emit(f"{decl_type} {var} = {rhs};")
                self.vars.add(var)
                self.set_type_name(var, c_type)

    def visit_AugAssign(self, node: ast.AugAssign):
        target = self.visit(node.target)
        op = BINOP_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该增强赋值运算")
        if isinstance(node.target, ast.Name):
            var = node.target.id
            explicit_globals = self.explicit_global_stack[-1]
            if self.in_wrapper_stack[-1] and var in self.global_vars:
                val = self.visit(node.value)
                self.emit(f"{var} {op}= {val};")
                return
            if var in explicit_globals and var not in self.global_vars:
                raise TranslateError(f"global 变量 {var} 未在顶层定义")
            if var in self.global_vars and var not in self.vars and var not in explicit_globals:
                raise TranslateError(
                    f"变量 {var} 在函数内被赋值但未声明 global，Python 语义为局部变量"
                )
        val = self.visit(node.value)
        self.emit(f"{target} {op}= {val};")

    def visit_If(self, node: ast.If):
        cond = self.visit(node.test)
        self.emit(f"if ({cond}) {{")
        self.indent()
        for s in node.body:
            self.visit(s)
        self.dedent()
        if node.orelse:
            self.emit("} else {")
            self.indent()
            for s in node.orelse:
                self.visit(s)
            self.dedent()
        self.emit("}")

    def visit_For(self, node: ast.For):
        if not (isinstance(node.target, ast.Name)
                and isinstance(node.iter, ast.Call)
                and isinstance(node.iter.func, ast.Name)
                and node.iter.func.id == "range"):
            raise TranslateError("仅支持 for x in range(...)")

        var = node.target.id
        args = node.iter.args
        if len(args) == 1:
            start, stop, step = "0", self.visit(args[0]), "1"
        elif len(args) == 2:
            start, stop, step = self.visit(args[0]), self.visit(args[1]), "1"
        elif len(args) == 3:
            start, stop, step = self.visit(args[0]), self.visit(args[1]), self.visit(args[2])
        else:
            raise TranslateError("range 参数数量不支持")

        if var not in self.vars:
            self.vars.add(var)
            self.emit(f"int {var};")
        init = f"{var} = {start}"

        # step < 0 时用 >
        cmp_op = "<"
        is_neg_step_lit = False
        if len(args) == 3:
            s2 = args[2]
            if isinstance(s2, ast.Constant) and isinstance(s2.value, int) and s2.value < 0:
                is_neg_step_lit = True
            elif isinstance(s2, ast.UnaryOp) and isinstance(s2.op, ast.USub) and isinstance(s2.operand, ast.Constant) and isinstance(s2.operand.value, int):
                # e.g. range(..., -1)
                is_neg_step_lit = True
        if is_neg_step_lit:
            cmp_op = ">"

        self.emit(f"for ({init}; {var} {cmp_op} {stop}; {var} += {step}) {{")
        self.indent()
        for s in node.body:
            self.visit(s)
        self.dedent()
        self.emit("}")



    def visit_While(self, node: ast.While):
        # Support: while <cond>:
        # (else: branch not supported)
        if node.orelse:
            raise TranslateError("暂不支持 while-else")
        cond = self.visit(node.test)
        self.emit(f"while ({cond}) {{")
        self.indent()
        for s in node.body:
            self.visit(s)
        self.dedent()
        self.emit("}")

    def visit_Break(self, node: ast.Break):
            self.emit("break;")

    def visit_Continue(self, node: ast.Continue):
        self.emit("continue;")

    # ----- expressions -----
    def visit_Call(self, node: ast.Call) -> str:
        # print
        if isinstance(node.func, ast.Name) and node.func.id == "print":
            if len(node.args) < 1:
                raise TranslateError("print 至少需要 1 个参数")
            # print(...) 在语句上下文由 visit_Expr 处理；这里仅保留单参情形以兼容旧逻辑
            if len(node.args) != 1:
                raise TranslateError("print 多参数仅支持作为语句使用")
            a0 = node.args[0]
            if isinstance(a0, ast.Constant) and isinstance(a0.value, str):
                s = self._escape_c_string(a0.value)
                return f'printf("{s}\\n")'
            return f'printf("%d\\n", {self.visit(a0)})'

        # len(name)
        if isinstance(node.func, ast.Name) and node.func.id == "len":
            if len(node.args) != 1 or not isinstance(node.args[0], ast.Name):
                raise TranslateError("len 仅支持 len(name)")
            name = node.args[0].id
            ln = self.get_len_name(name)
            if not ln:
                raise TranslateError(f"len({name}) 无法解析长度变量（请先用 list 字面量初始化或传入数组形参）")
            return ln

        if isinstance(node.func, ast.Name):
            func_name = self.func_name_alias.get(node.func.id, node.func.id)
        else:
            func_name = self.visit(node.func)
        # Auto-insert length args for known array parameters when missing.
        if isinstance(node.func, ast.Name):
            callee = node.func.id
            arr_params = self.func_array_params.get(callee, [])
            if arr_params:
                fn_args = self.func_arg_names.get(callee, [])
                len_param_map = {ln: arr for arr, ln in arr_params}
                len_param_set = set(len_param_map.keys())

                appended_len = [ln for _, ln in arr_params if ln not in fn_args]
                call_param_order = fn_args + appended_len

                if len(node.args) < len(call_param_order):
                    call_args: List[str] = []
                    idx_in = 0
                    for i, param in enumerate(call_param_order):
                        remaining_args = len(node.args) - idx_in
                        remaining_non_len = sum(
                            1 for p in call_param_order[i:] if p not in len_param_set
                        )
                        if param in len_param_set and remaining_args == remaining_non_len:
                            arr_param = len_param_map[param]
                            arr_idx = fn_args.index(arr_param) if arr_param in fn_args else None
                            if arr_idx is None or arr_idx >= len(node.args):
                                raise TranslateError(
                                    f"call {callee} missing array arg for length '{param}'"
                                )
                            arr_arg = node.args[arr_idx]
                            len_expr = None
                            if isinstance(arr_arg, ast.Name):
                                len_expr = self.get_len_name(arr_arg.id)
                            if not len_expr:
                                raise TranslateError(
                                    f"cannot resolve length for arg '{arr_param}' in call {callee}()"
                                )
                            call_args.append(len_expr)
                            continue

                        if idx_in < len(node.args):
                            call_args.append(self.visit(node.args[idx_in]))
                            idx_in += 1
                            continue

                        if param in len_param_set:
                            arr_param = len_param_map[param]
                            arr_idx = fn_args.index(arr_param) if arr_param in fn_args else None
                            if arr_idx is None or arr_idx >= len(node.args):
                                raise TranslateError(
                                    f"call {callee} missing array arg for length '{param}'"
                                )
                            arr_arg = node.args[arr_idx]
                            len_expr = None
                            if isinstance(arr_arg, ast.Name):
                                len_expr = self.get_len_name(arr_arg.id)
                            if not len_expr:
                                raise TranslateError(
                                    f"cannot resolve length for arg '{arr_param}' in call {callee}()"
                                )
                            call_args.append(len_expr)
                            continue

                        raise TranslateError(f"call {callee} missing required argument '{param}'")

                    return f"{func_name}({', '.join(call_args)})"

        args = [self.visit(a) for a in node.args]
        return f"{func_name}({', '.join(args)})"


    def visit_Compare(self, node: ast.Compare) -> str:
        if len(node.ops) != 1 or len(node.comparators) != 1:
            raise TranslateError("仅支持简单比较")
        op = CMPOP_MAP.get(type(node.ops[0]))
        if not op:
            raise TranslateError("不支持该比较运算")
        return f"({self.visit(node.left)} {op} {self.visit(node.comparators[0])})"

    
    def visit_BinOp(self, node: ast.BinOp) -> str:
        # Special-case: modulo 2**32 -> cast to uint32_t (wraparound)
        if isinstance(node.op, ast.Mod) and isinstance(node.right, ast.Constant) and isinstance(node.right.value, int):
            if node.right.value == 4294967296:
                inner = node.left
                # Special-case common LCG pattern: (x*1664525 + 1013904223) % 2**32
                if isinstance(inner, ast.BinOp) and isinstance(inner.op, ast.Add) and isinstance(inner.right, ast.Constant) and inner.right.value == 1013904223:
                    mul = inner.left
                    if isinstance(mul, ast.BinOp) and isinstance(mul.op, ast.Mult) and isinstance(mul.right, ast.Constant) and mul.right.value == 1664525:
                        # x may be name or expr
                        xexpr = self.visit(mul.left)
                        return f"((uint32_t)((uint64_t)({xexpr}) * 1664525u + 1013904223u))"
                return f"((uint32_t)({self.visit(node.left)}))"

        if isinstance(node.op, ast.Div):
            left = self.visit(node.left)
            right = self.visit(node.right)
            return f"((double)({left}) / (double)({right}))"

        if isinstance(node.op, ast.FloorDiv):
            left = self.visit(node.left)
            right = self.visit(node.right)
            return f"((int)floor((double)({left}) / (double)({right})))"

        op = BINOP_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该二元运算")
        return f"({self.visit(node.left)} {op} {self.visit(node.right)})"

    def visit_UnaryOp(self, node: ast.UnaryOp) -> str:
        op = UNARY_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该一元运算")
        return f"({op}{self.visit(node.operand)})"

    def visit_BoolOp(self, node: ast.BoolOp) -> str:
        if isinstance(node.op, ast.And):
            op = "&&"
        elif isinstance(node.op, ast.Or):
            op = "||"
        else:
            raise TranslateError("不支持该布尔运算")
        return "(" + f" {op} ".join(self.visit(v) for v in node.values) + ")"

    def visit_Subscript(self, node: ast.Subscript) -> str:
        if not isinstance(node.value, ast.Name):
            raise TranslateError("仅支持 name[index]")
        base = self.get_renamed_param(node.value.id)  # 应用参数重映射
        idx = self.visit(node.slice)
        return f"{base}[{idx}]"

    def visit_Name(self, node: ast.Name) -> str:
        # 如果参数名被重映射（为避免全局变量遮蔽），使用重映射后的名称
        return self.get_renamed_param(node.id)

    def visit_Constant(self, node: ast.Constant) -> str:
        if isinstance(node.value, str):
            s = self._escape_c_string(node.value)
            return f"\"{s}\""
        if isinstance(node.value, float):
            return str(node.value) # 或 f"{node.value:g}"
        if node.value is True:
            return "1"
        if node.value is False:
            return "0"
        if node.value is None:
            return "0"
        return str(node.value)

    def generic_visit(self, node):
        raise TranslateError(f"不支持的语法节点: {type(node).__name__}（{self.filename}:{getattr(node,'lineno','?')}）")

def collect_pyfiles(paths: List[str]) -> List[str]:
    files: List[str] = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, fnames in os.walk(p):
                for fn in fnames:
                    if fn.endswith(".py"):
                        files.append(os.path.join(root, fn))
        elif p.endswith(".py"):
            files.append(p)
    return sorted(set(files))

def syntax_check(pyfiles: List[str], out_path: str) -> List[str]:
    ok: List[str] = []
    bad: List[str] = []
    lines: List[str] = []
    for f in pyfiles:
        try:
            py_compile.compile(f, doraise=True)
            ok.append(f)
            lines.append(f"✅ 语法检查通过: {f}")
        except py_compile.PyCompileError as e:
            bad.append(f)
            lines.append(f"❌ 语法检查失败: {f}")
            lines.append(str(e))
        except Exception as e:
            bad.append(f)
            lines.append(f"❌ 语法检查异常: {f}")
            lines.append(repr(e))
    lines.append("")
    lines.append(f"汇总: 通过 {len(ok)} 个, 失败 {len(bad)} 个")
    _force_write_text(out_path, "\n".join(lines))
    return ok

def translate_files(pyfiles: List[str], out_dir: str, out_path: str) -> None:
    os.makedirs(out_dir, exist_ok=True)
    ok = 0
    bad = 0
    lines: List[str] = []
    for f in pyfiles:
        try:
            src = open(f, "r", encoding="utf-8").read()
            tree = ast.parse(src, filename=f)
            c_code = CCodeGenerator(filename=f).generate(tree)
            base = os.path.splitext(os.path.basename(f))[0]
            c_file = os.path.join(out_dir, f"pyc_{base}.c")
            _force_write_text(c_file, c_code)
            ok += 1
            lines.append(f"✅ 转换成功: {f} -> {c_file}")
        except Exception as e:
            bad += 1
            lines.append(f"❌ 转换失败: {f}")
            lines.append(repr(e))
    lines.append("")
    lines.append(f"汇总: 成功 {ok} 个, 失败 {bad} 个")
    _force_write_text(out_path, "\n".join(lines))

def main():
    parser = argparse.ArgumentParser(description="Translate Python subset to C (enhanced list->array & len handling)")
    parser.add_argument("pyfiles", nargs="*", default=["./PyFiles"], help="Python files/directories (default: ./PyFiles)")
    args = parser.parse_args()

    default_dir = "./PyFiles"
    if not os.path.exists(default_dir):
        print(f"错误：默认目录 '{default_dir}' 不存在！请先创建该文件夹并放入.py文件", file=sys.stderr)
        sys.exit(1)

    pyfiles = collect_pyfiles(args.pyfiles)
    if not pyfiles:
        print("未找到 .py 文件。", file=sys.stderr)
        sys.exit(1)

    ok_files = syntax_check(pyfiles, "result_1.txt")
    translate_files(ok_files, "PythonC", "result_2.txt")

if __name__ == "__main__":
    main()
