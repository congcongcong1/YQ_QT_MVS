# -*- coding: utf-8 -*-
import ast
import sys
import argparse
import os
import py_compile
import traceback
from typing import Dict, List, Optional, Tuple, Set
from datetime import datetime
import time


BINOP_MAP = {
    ast.Add: "+",
    ast.Sub: "-",
    ast.Mult: "*",
    ast.Div: "/",
    ast.FloorDiv: "/",
    ast.Mod: "%",
    ast.BitAnd: "&",
    ast.BitOr: "|",
    ast.BitXor: "^",
    ast.LShift: "<<",
    ast.RShift: ">>",
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
    ast.Invert: "~",
}

MATH_FUNC_MAP = {
    'sin': 'sin',
    'cos': 'cos',
    'tan': 'tan',
    'asin': 'asin',
    'acos': 'acos',
    'atan': 'atan',
    'atan2': 'atan2',
    'sinh': 'sinh',
    'cosh': 'cosh',
    'tanh': 'tanh',
    'exp': 'exp',
    'log': 'log',
    'log10': 'log10',
    'pow': 'pow',
    'sqrt': 'sqrt',
    'ceil': 'ceil',
    'floor': 'floor',
    'fabs': 'fabs',
}

MATH_CONST_MAP = {
    'pi': '3.14159265358979323846',
    'e': '2.71828182845904523536',
}

# Transpiler version
VERSION = "1.23"


class TranspilationStats:
    """Track statistics during Python-to-C transpilation.
    
    Collects metrics about the transpilation process including:
    - Number of functions, classes, and global variables
    - Lines of code (Python vs C)
    - Type usage statistics
    - Transpilation time
    """
    def __init__(self):
        self.num_functions = 0
        self.num_classes = 0
        self.num_global_vars = 0
        self.python_lines = 0
        self.c_lines = 0
        self.type_counts: Dict[str, int] = {}
        self.start_time = 0.0
        self.end_time = 0.0
    
    def start(self):
        """Start timing the transpilation."""
        self.start_time = time.time()
    
    def end(self):
        """End timing the transpilation."""
        self.end_time = time.time()
    
    def elapsed_time(self) -> float:
        """Get elapsed time in seconds."""
        return self.end_time - self.start_time
    
    def record_type(self, type_name: str):
        """Record usage of a C type."""
        self.type_counts[type_name] = self.type_counts.get(type_name, 0) + 1
    
    def format_summary(self) -> str:
        """Format statistics as a human-readable summary."""
        lines = []
        lines.append("=" * 60)
        lines.append("转换统计信息")
        lines.append("=" * 60)
        lines.append(f"函数数量: {self.num_functions}")
        lines.append(f"类数量: {self.num_classes}")
        lines.append(f"全局变量数量: {self.num_global_vars}")
        lines.append(f"Python 代码行数: {self.python_lines}")
        lines.append(f"生成的 C 代码行数: {self.c_lines}")
        if self.c_lines > 0 and self.python_lines > 0:
            ratio = self.c_lines / self.python_lines
            lines.append(f"代码膨胀率: {ratio:.2f}x")
        if self.end_time > self.start_time:
            lines.append(f"转换耗时: {self.elapsed_time():.3f} 秒")
        if self.type_counts:
            lines.append("\n类型使用统计:")
            for type_name, count in sorted(self.type_counts.items(), key=lambda x: -x[1]):
                lines.append(f"  {type_name}: {count}")
        lines.append("=" * 60)
        return "\n".join(lines)

class TranslateError(Exception):
    """Custom exception for Python-to-C transpilation errors.
    
    Captures error messages along with source location information (line number
    and column offset) from the AST node where the error occurred.
    """
    def __init__(self, message: str, node: ast.AST = None):
        """Initialize a transpilation error with message and optional AST node.
        
        Args:
            message: Human-readable error description
            node: Optional AST node where the error occurred (for location tracking)
        """
        super().__init__(message)
        self.message = message
        self.lineno = getattr(node, 'lineno', None) if node else None
        self.col_offset = getattr(node, 'col_offset', None) if node else None
    
    def __str__(self):
        """Format error message with line number if available.
        
        Returns:
            Error message string, optionally including line number
        """
        if self.lineno:
            return f"{self.message} (行 {self.lineno})"
        return self.message


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
    """Python-to-C transpiler that converts a restricted Python subset to C code.
    
    This transpiler handles:
    - Basic data types: int, double, bool, char*, uint32_t, uint16_t
    - Arrays (1D and 2D) with automatic length tracking
    - Functions with type inference
    - Control flow: if/else, for, while, break, continue
    - Structs (from Python classes)
    - Math operations and standard library functions
    """
    def __init__(self, filename: str, source_text: str | None = None):
        """Initialize the C code generator.
        
        Args:
            filename: Path to the Python source file being transpiled
            source_text: Optional source code text (if None, reads from filename)
        """
        self.filename = filename
        if source_text is None:
            try:
                with open(filename, "r", encoding="utf-8", errors="ignore") as fp:
                    source_text = fp.read()
            except Exception:
                source_text = None
        self.source_text = source_text
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
        self.width_stack: List[Dict[str, str]] = [dict()]
        self.array2d_stack: List[Set[str]] = [set()]

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
        self.func_array2d_params: Dict[str, List[Tuple[str, str]]] = {}
        # func -> {原始参数名 -> C参数名}，用于调用处翻译参数
        self.func_param_rename_map: Dict[str, Dict[str, str]] = {}
        # func -> [arg1, arg2, ...] (original python arg order)
        self.func_arg_names: Dict[str, List[str]] = {}
        self.func_ret_types: Dict[str, str] = {}

        self._top_level_stmts: List[ast.stmt] = []
        self.struct_defs: Dict[str, List[str]] = {}
        self.struct_names: Set[str] = set()
        self.needs_str_concat: bool = False
        self.needs_mem32: bool = False
        self.external_macro_prefixes = (
            # "GPIO_",
            # "GPIO", # 支持 GPIOA, GPIOB 等直接连接名字的形式
            # "RCC_",
            # "TIM_",
            # "EXTI_",
            # "DMA_",
            # "ADC_",
        )
        self.external_struct_names: Set[str] = set()

    def _is_external_macro_name(self, name: str) -> bool:
        """Check if a name should be treated as an external macro/constant.
        
        Args:
            name: Variable or constant name to check
            
        Returns:
            True if the name matches external macro prefixes (e.g., GPIO_, RCC_)
        """
        if not name:
            return False
        # 不再盲目将所有全大写名字视为外部宏
        # 仅仅依赖前缀来判断，这样用户自定义的大写常量（如 STATE_IDLE）可以被正确导出为 C 全局变量
        return any(name.startswith(p) for p in self.external_macro_prefixes)

    def _get_source_segment(self, node: ast.AST) -> str | None:
        """Extract the original source code text for an AST node.
        
        Args:
            node: AST node to extract source for
            
        Returns:
            Source code string, or None if unavailable
        """
        if not self.source_text:
            return None
        try:
            return ast.get_source_segment(self.source_text, node)
        except Exception:
            return None

    def _normalize_int_literal(self, text: str) -> str:
        """Normalize integer literal text by removing parentheses and underscores.
        
        Converts Python octal literals (0o...) to C format (0...).
        
        Args:
            text: Raw integer literal string from source
            
        Returns:
            Normalized integer literal suitable for C code
        """
        s = text.strip()
        while s.startswith("(") and s.endswith(")") and len(s) > 2:
            s = s[1:-1].strip()
        s = s.replace("_", "")
        if s.lower().startswith("0o"):
            return "0" + s[2:]
        return s

    def _normalize_hex_literal(self, text: str) -> str:
        """Normalize hexadecimal literal text.
        
        Args:
            text: Raw hex literal string from source
            
        Returns:
            Normalized hex literal suitable for C code
        """
        return self._normalize_int_literal(text)

    def _is_hex_literal(self, node: ast.AST) -> bool:
        """Check if an AST node represents a hexadecimal literal.
        
        Args:
            node: AST node to check
            
        Returns:
            True if the node is a hex literal (e.g., 0x1234)
        """
        if not isinstance(node, ast.Constant) or not isinstance(node.value, int):
            return False
        segment = self._get_source_segment(node)
        if not segment:
            return False
        s = self._normalize_int_literal(segment)
        return s.lower().startswith("0x")

    def _is_octal_literal(self, node: ast.AST) -> bool:
        """Check if an AST node represents an octal literal.
        
        Args:
            node: AST node to check
            
        Returns:
            True if the node is an octal literal (e.g., 0o755)
        """
        if not isinstance(node, ast.Constant) or not isinstance(node.value, int):
            return False
        segment = self._get_source_segment(node)
        if not segment:
            return False
        s = segment.strip()
        while s.startswith("(") and s.endswith(")") and len(s) > 2:
            s = s[1:-1].strip()
        s = s.replace("_", "")
        return s.lower().startswith("0o")

    def _is_uint16_literal(self, node: ast.AST) -> bool:
        """Check if an AST node represents a uint16_t literal.
        
        A literal is considered uint16_t if it's a hex or octal value <= 0xFFFF.
        
        Args:
            node: AST node to check
            
        Returns:
            True if the node should be typed as uint16_t
        """
        if not isinstance(node, ast.Constant) or not isinstance(node.value, int):
            return False
        if node.value > 0xFFFF:
            return False
        return self._is_hex_literal(node) or self._is_octal_literal(node)

    # ----- type helpers -----
    def set_type_name(self, var: str, type_name: str) -> None:
        """Record the inferred C type for a variable in the current scope.

        type_name values used in this transpiler:
        - 'int'
        - 'double'
        - 'char *'
        - 'bool'
        - 'uint32_t'
        - 'uint16_t'
        - 'int_arr'  (int array / pointer)
        - 'int_arr2d' (2D int array flattened)
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
            if self._parse_2d_list_literal(v):
                return True
            return all(isinstance(e, ast.Constant) and isinstance(e.value, (int, float, str)) for e in v.elts)
        if isinstance(v, ast.Constant) and isinstance(v.value, (int, float, str)):
            return True
        return False

    def _parse_2d_list_literal(self, node: ast.List) -> Tuple[int, int, List[str], str] | None:
        """Parse a 2D list literal into flattened C array representation.
        
        Args:
            node: AST List node potentially containing nested lists
            
        Returns:
            Tuple of (rows, cols, flat_values, element_type) if 2D list, None otherwise
            - rows: Number of rows
            - cols: Number of columns (must be uniform across rows)
            - flat_values: Flattened list of C code strings for each element
            - element_type: Inferred C type ('int' or 'double')
        """
        if not node.elts:
            return None
        if not all(isinstance(e, ast.List) for e in node.elts):
            return None
        rows = len(node.elts)
        cols: int | None = None
        flat: List[str] = []
        elem_type: str | None = None
        
        for row in node.elts:
            assert isinstance(row, ast.List)
            if cols is None:
                cols = len(row.elts)
            elif len(row.elts) != cols:
                raise TranslateError("2D list rows must have the same length", node)
            for e in row.elts:
                # 针对每个元素调用 visit 获取 C 代码，并用 _get_expr_type 获取类型
                val_str = self.visit(e)
                t = self._get_expr_type(e)
                if elem_type is None:
                    elem_type = t
                elif elem_type == "int" and t == "double":
                    elem_type = "double"
                
                flat.append(val_str)
        
        if cols is None:
            cols = 0
        if elem_type is None:
            elem_type = "int"
            
        return rows, cols, flat, elem_type

    def _parse_class_struct(self, node: ast.ClassDef) -> List[str]:
        """Extract struct field names from a Python class definition.
        
        Analyzes the __init__ method to find all self.field assignments.
        
        Args:
            node: ClassDef AST node
            
        Returns:
            List of field names found in __init__
            
        Raises:
            TranslateError: If class has no __init__ or no field assignments
        """
        init_fn: ast.FunctionDef | None = None
        for s in node.body:
            if isinstance(s, ast.FunctionDef) and s.name == "__init__":
                init_fn = s
                break
        if init_fn is None:
            raise TranslateError(f"class {node.name} missing __init__ for struct mapping")
        fields: List[str] = []
        for s in init_fn.body:
            if isinstance(s, ast.Assign) and len(s.targets) == 1:
                t = s.targets[0]
                if isinstance(t, ast.Attribute) and isinstance(t.value, ast.Name) and t.value.id == "self":
                    if t.attr not in fields:
                        fields.append(t.attr)
        if not fields:
            raise TranslateError(f"class {node.name} has no self.<field> assignments")
        return fields

    def _parse_namedtuple_def(self, node: ast.Assign) -> Tuple[str, List[str]] | None:
        """Parse a namedtuple definition into struct-compatible format.
        
        Recognizes patterns like:
            Point = namedtuple('Point', ['x', 'y'])
            Point = namedtuple('Point', 'x y')
        
        Args:
            node: Assignment AST node potentially defining a namedtuple
            
        Returns:
            Tuple of (typename, field_list) if valid namedtuple, None otherwise
            
        Raises:
            TranslateError: If namedtuple syntax is invalid
        """
        if len(node.targets) != 1 or not isinstance(node.targets[0], ast.Name):
            return None
        if not isinstance(node.value, ast.Call):
            return None
        func = node.value.func
        if isinstance(func, ast.Attribute):
            if func.attr != "namedtuple":
                return None
        elif isinstance(func, ast.Name):
            if func.id != "namedtuple":
                return None
        else:
            return None
        if len(node.value.args) < 2:
            raise TranslateError("namedtuple requires (typename, fields)", node)
        fields_arg = node.value.args[1]
        fields: List[str] = []
        if isinstance(fields_arg, (ast.List, ast.Tuple)):
            for e in fields_arg.elts:
                if not isinstance(e, ast.Constant) or not isinstance(e.value, str):
                    raise TranslateError("namedtuple fields must be string literals")
                fields.append(e.value)
        elif isinstance(fields_arg, ast.Constant) and isinstance(fields_arg.value, str):
            raw = fields_arg.value.replace(",", " ")
            fields = [f for f in raw.split() if f]
        else:
            raise TranslateError("namedtuple fields must be list/tuple of strings or a string")
        return node.targets[0].id, fields

    def _emit_global_assign(self, node: ast.Assign) -> None:
        """Emit a C global declaration for a supported top-level assign."""
        assert len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
        var = node.targets[0].id
        val = node.value
        if self._is_external_macro_name(var):
            # Assume macro/constant defined in external headers.
            return

        if var in self.global_vars:
            # C 文件级别不允许重复定义；如果用户重复写同名全局，这里直接报错更清晰
            raise TranslateError(f"顶层全局变量 {var} 重复定义（C 不允许重复定义全局变量）", node)

        # list literal -> arr[] + arr_len
        if isinstance(val, ast.List):
            parsed_2d = self._parse_2d_list_literal(val)
            if parsed_2d:
                rows, cols, flat, elem_type = parsed_2d
                init = "{ " + ", ".join(flat) + " }"
                self.emit(f"{elem_type} {var}[] = {init};")
                len_var = f"{var}_h"
                width_var = f"{var}_w"
                self.emit(f"int {len_var} = {rows};")
                self.emit(f"int {width_var} = {cols};")
                self.global_vars.add(var)
                self.global_vars.add(len_var)
                self.global_vars.add(width_var)
                self.vars_stack[0].add(var)
                self.vars_stack[0].add(len_var)
                self.vars_stack[0].add(width_var)
                self.len_stack[0][var] = len_var
                self.width_stack[0][var] = width_var
                self.array2d_stack[0].add(var)
                self.type_stack[0][var] = f"{elem_type}_arr2d"
                self.type_stack[0][len_var] = "int"
                self.type_stack[0][width_var] = "int"
                return

            # 根据第一个元素推断一维数组类型，默认为 int
            elem_type = "int"
            if val.elts:
                elem_type = self._get_expr_type(val.elts[0])
            
            elems = [self.visit(e) for e in val.elts] 
            n = len(elems)
            init = "{ " + ", ".join(elems) + " }"
            self.emit(f"{elem_type} {var}[] = {init};")
            len_var = f"{var}_len"
            self.emit(f"int {len_var} = {n};")
            # 记录到“全局集合”和“长度映射（基作用域）”
            self.global_vars.add(var)
            self.global_vars.add(len_var)
            self.vars_stack[0].add(var)
            self.vars_stack[0].add(len_var)
            self.len_stack[0][var] = len_var
            self.type_stack[0][var] = f"{elem_type}_arr"
            self.type_stack[0][len_var] = "int"
            return

        # scalar constants
        if isinstance(val, ast.Constant):
            c_type = self._get_expr_type(val)
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
        """Get the set of declared variables in the current scope.
        
        Returns:
            Set of variable names declared in the innermost scope
        """
        return self.vars_stack[-1]

    def push_scope(self):
        """Enter a new scope (function body, loop, etc.).
        
        Creates new scope frames for variables, types, array metadata, and parameters.
        Child scopes inherit parent scope's type and array information.
        """
        self.vars_stack.append(set())
        self.len_stack.append(dict(self.len_stack[-1]))
        self.width_stack.append(dict(self.width_stack[-1]))
        self.array2d_stack.append(set(self.array2d_stack[-1]))
        self.type_stack.append(dict(self.type_stack[-1]))
        self.param_rename_stack.append(dict())
        self.predecl_type_stack.append(dict())
        self.explicit_global_stack.append(set())
        self.param_name_stack.append(set())
        self.in_wrapper_stack.append(False)

    def pop_scope(self):
        """Exit the current scope and return to the parent scope.
        
        Discards all scope-specific tracking information for the exited scope.
        """
        self.vars_stack.pop()
        self.len_stack.pop()
        self.width_stack.pop()
        self.array2d_stack.pop()
        self.type_stack.pop()
        self.param_rename_stack.pop()
        self.predecl_type_stack.pop()
        self.explicit_global_stack.pop()
        self.param_name_stack.pop()
        self.in_wrapper_stack.pop()

    def _merge_types(self, cur: str | None, new: str) -> str:
        """Merge two C type names, preferring the more general type.
        
        Type precedence (most to least general):
        int_arr2d > int_arr > double > uint32_t > uint16_t > int
        
        Args:
            cur: Current type (or None)
            new: New type to merge
            
        Returns:
            The merged type that can represent both inputs
        """
        if not cur:
            return new
        if cur == new:
            return cur
        if cur == "int_arr2d" or new == "int_arr2d":
            return "int_arr2d"
        if cur == "int_arr" or new == "int_arr":
            return "int_arr"
        if cur == "double" or new == "double":
            return "double"
        if cur == "uint32_t" or new == "uint32_t":
            return "uint32_t"
        if cur == "uint16_t" or new == "uint16_t":
            return "uint16_t"
        return cur

    def _decl_type(self, c_type: str) -> str:
        """Convert internal type representation to C declaration type.
        
        Args:
            c_type: Internal type name (e.g., 'int_arr', 'int_arr2d', 'int')
            
        Returns:
            C declaration type (e.g., 'int *' for arrays, 'int' for scalars)
        """
        if c_type in ("int_arr", "int_arr2d"):
            return "int *"
        return c_type

    def set_len_name(self, var: str, len_var: str):
        """Associate an array variable with its length variable name.
        
        Args:
            var: Array variable name
            len_var: Name of the variable holding the array's length
        """
        self.len_stack[-1][var] = len_var

    def get_len_name(self, var: str) -> Optional[str]:
        """Retrieve the length variable name for an array.
        
        Args:
            var: Array variable name
            
        Returns:
            Length variable name, or None if not an array
        """
        return self.len_stack[-1].get(var)
    
    def set_width_name(self, var: str, width_var: str):
        """Associate a 2D array variable with its width variable name.
        
        Args:
            var: 2D array variable name
            width_var: Name of the variable holding the array's width (columns)
        """
        self.width_stack[-1][var] = width_var

    def get_width_name(self, var: str) -> Optional[str]:
        """Retrieve the width variable name for a 2D array.
        
        Args:
            var: 2D array variable name
            
        Returns:
            Width variable name, or None if not a 2D array
        """
        return self.width_stack[-1].get(var)

    def mark_array2d(self, var: str):
        """Mark a variable as a 2D array.
        
        Args:
            var: Variable name to mark as 2D array
        """
        self.array2d_stack[-1].add(var)

    def is_array2d(self, var: str) -> bool:
        """Check if a variable is marked as a 2D array.
        
        Args:
            var: Variable name to check
            
        Returns:
            True if variable is a 2D array
        """
        return any(var in scope for scope in reversed(self.array2d_stack))

    def fresh_var(self, base: str) -> str:
        """Generate a new variable name that doesn't collide in current scope."""
        name = base
        k = 0
        while name in self.vars:
            k += 1
            name = f"{base}{k}"
        return name
    
    def get_renamed_param(self, param_name: str) -> str:
        """Get the C-renamed parameter name, handling global variable shadowing.
        
        Args:
            param_name: Original Python parameter name
            
        Returns:
            Renamed C parameter name, or original name if no renaming needed
        """
        return self.param_rename_stack[-1].get(param_name, param_name)
    
    def _get_expr_type(self, node: ast.AST) -> str:
        """Recursively infer the C type of a Python expression.
        
        Analyzes AST nodes to determine appropriate C types:
        - Constants: int, double, char*, bool based on Python type
        - Variables: Lookup from type_stack
        - Lists: int_arr or int_arr2d with element type
        - BinOp: Type promotion rules (e.g., int + double -> double)
        - Calls: Return type from function registry or built-ins
        
        Args:
            node: AST expression node to analyze
            
        Returns:
            C type string (e.g., 'int', 'double', 'char *', 'int_arr', 'uint32_t')
        """
        if isinstance(node, ast.Constant):
            val = node.value
            if isinstance(val, float): 
                return "double"
            if isinstance(val, str): 
                return "char *"
            if isinstance(val, bool): 
                return "bool"
            if isinstance(val, int):
                # 针对单片机地址优化的逻辑：
                # 如果是十六进制字面量（通常是基地址），且大于 0xFFFF，优先推断为 uint32_t
                if self._is_hex_literal(node) and val > 0xFFFF:
                    return "uint32_t"
                if val > 4294967295 or val < -2147483648:
                    return "int64_t"
                if val > 2147483647:
                    return "uint32_t"
                if self._is_uint16_literal(node):
                    return "uint16_t"
                return "int"
            return "int"
        
        if isinstance(node, ast.Name):
            # 从作用域栈中查找已知变量的类型
            t = self.get_type_name(node.id)
            return t if t else "int"

        if isinstance(node, ast.List):
            parsed_2d = self._parse_2d_list_literal(node)
            if parsed_2d:
                _, _, _, etype = parsed_2d
                return f"{etype}_arr2d"
            etype = "int"
            if node.elts:
                etype = self._get_expr_type(node.elts[0])
            return f"{etype}_arr"

        if isinstance(node, ast.Subscript):
            atype = self._get_expr_type(node.value)
            # 如果 atype 是数组类型（带 _arr 或 _arr2d），提取其基础类型
            if "_arr" in atype:
                return atype.split("_arr")[0]
            # 如果已经是标量类型，直接返回
            return atype

        if isinstance(node, ast.BinOp):
            if isinstance(node.op, ast.Add):
                lt = self._get_expr_type(node.left)
                rt = self._get_expr_type(node.right)
                if lt == "char *" or rt == "char *":
                    return "char *"
            if isinstance(node.op, ast.Mod):
                if isinstance(node.right, ast.Constant) and node.right.value == 4294967296:
                    return "uint32_t"
            # 只要左右操作数有一个是 double，结果就是 double
            if isinstance(node.op, ast.Pow):
                return "double"
            if isinstance(node.op, ast.Div):
                return "double"
            if isinstance(node.op, ast.FloorDiv):
                return "int"
            lt = self._get_expr_type(node.left)
            rt = self._get_expr_type(node.right)
            if lt == "double" or rt == "double":
                return "double"
            return "int"

        if isinstance(node, (ast.Compare, ast.BoolOp)):
            return "bool"

        if isinstance(node, ast.Attribute):
            # 优先处理 math 常量
            if isinstance(node.value, ast.Name) and node.value.id == "math":
                if node.attr in MATH_CONST_MAP:
                    return "double"
            
            # 递归获取 base 的类型，并从结构体定义中查找字段类型
            t = self._get_expr_type(node.value)
            if t in self.struct_names:
                fields = self.struct_defs.get(t, {})
                return fields.get(node.attr, "int")
            return "int"

        if isinstance(node, ast.Call):
            # math functions
            if isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) and node.func.value.id == "math":
                return "double"
            # 如果是调用已知函数，查找其返回类型
            if isinstance(node.func, ast.Name):
                fid = node.func.id
                if fid in self.struct_names:
                    return fid
                if fid == "int":
                    return "int"
                if fid == "float":
                    return "double"
                if fid == "bool":
                    return "bool"
                if fid == "width":
                    return "int"
                if fid == "abs":
                    return self._get_expr_type(node.args[0])
                if fid == "len":
                    return "int"
                return self.func_ret_types.get(fid, "int")

        if isinstance(node, ast.UnaryOp):
            # 处理单目操作符（如 -x 或 not x）
            operand_type = self._get_expr_type(node.operand)
            if operand_type == "double":
                return "double"
            return "int"

        return "int"

    def _get_c_type_from_annotation(self, ann: ast.AST | None) -> str:
        """Convert Python type annotation to C type string.
        
        Supports:
        - Basic types: int, float/double, bool, str -> char*
        - Sized integers: uint32_t, uint16_t, int64_t
        - Lists: List[int] -> int*, List[float] -> double*
        
        Args:
            ann: Type annotation AST node (or None)
            
        Returns:
            C type string, defaults to 'int' if annotation is None or unrecognized
        """
        if ann is None:
            return "int"
        if isinstance(ann, ast.Name):
            t_map = {
                "float": "double",
                "double": "double",
                "int": "int",
                "bool": "bool",
                "str": "char *",
                "uint32_t": "uint32_t",
                "uint16_t": "uint16_t",
                "int64_t": "int64_t",
                "List": "int *", # 默认
            }
            return t_map.get(ann.id, ann.id)
        if isinstance(ann, ast.Attribute):
            return ann.attr
        if isinstance(ann, ast.Subscript):
            # 处理 List[int], List[float] 等
            base = ann.value
            if isinstance(base, ast.Name) and base.id in ("List", "list"):
                etype = self._get_c_type_from_annotation(ann.slice)
                return f"{etype} *"
        return "int"

    def _infer_struct_param_type(self, fn: ast.FunctionDef, param_name: str) -> str | None:
        """Infer if a function parameter should be a struct type.
        
        Analyzes attribute accesses (param.field) within the function body
        to determine if the parameter matches a known struct definition.
        
        Args:
            fn: Function definition to analyze
            param_name: Parameter name to check
            
        Returns:
            Struct type name if uniquely determined, None otherwise
        """
        if not self.struct_defs:
            return None
        field_hits: List[str] = []
        for sub in ast.walk(fn):
            if isinstance(sub, ast.Attribute) and isinstance(sub.value, ast.Name):
                if sub.value.id != param_name:
                    continue
                for struct_name, fields in self.struct_defs.items():
                    if sub.attr in fields:
                        field_hits.append(struct_name)
        uniq = {n for n in field_hits}
        if len(uniq) == 1:
            return next(iter(uniq))
        return None
        
    def _get_print_info(self, node: ast.AST) -> Tuple[str, str, bool]:
        """Returns (format_spec, value_str, is_array)"""
        atype = self._get_expr_type(node)
        val_str = self.visit(node)
        is_arr = False
        
        if isinstance(node, ast.Name):
            if self.is_array2d(node.id) or self.get_len_name(node.id):
                is_arr = True
        
        fmt = "%d"
        if atype == "double":
            fmt = "%2f"
        elif atype == "char *":
            fmt = "%s"
        elif atype == "bool":
            fmt = "%s"
            # In emit_print_call, we usually want (val ? "True" : "False") for print() calls
            val_str = f"({val_str} ? \"True\" : \"False\")"
        elif atype == "uint32_t" or atype == "uint16_t":
            fmt = "%u"
        elif atype == "int64_t":
            fmt = "%lld"
            
        return fmt, val_str, is_arr
        
    def emit_print_call(self, node: ast.Call):
        """Generate C printf code for a Python print() call.
        
        Handles:
        - Empty print() -> printf("\n")
        - print(prefix, value) -> formatted output with type-specific format specifiers
        - print(arr) -> loop-based array printing (1D and 2D)
        - Multiple arguments -> space-separated output
        
        Args:
            node: Call AST node representing print(...)
        """
        args = list(node.args)

        if len(args) == 0:
            self.emit('printf("\\n");')
            return

        if len(args) == 2 and isinstance(args[0], ast.Constant) and isinstance(args[0].value, str):
            prefix_c = self._escape_c_string(args[0].value)
            a1 = args[1]
            fmt, val_str, is_arr = self._get_print_info(a1)

            if is_arr and isinstance(a1, ast.Name):
                atype = self._get_expr_type(a1)
                etype = atype.split("_arr")[0] if "_arr" in atype else "int"
                efmt = "%d"
                if etype == "double": efmt = "%2f"
                elif etype == "char *": efmt = "%s"
                elif "uint" in etype: efmt = "%u"
                
                self.emit(f'printf("%s", "{prefix_c} ");')
                c_name = self.get_renamed_param(a1.id)
                if self.is_array2d(a1.id):
                    h = self.get_len_name(a1.id)
                    w = self.get_width_name(a1.id)
                    iy = self.fresh_var("__y_print")
                    ix = self.fresh_var("__x_print")
                    self.emit('printf("[");')
                    self.emit(f'for (int {iy} = 0; {iy} < {h}; {iy}++) {{')
                    self.indent()
                    self.emit('if (%s > 0) printf(", ");' % iy)
                    self.emit('printf("[");')
                    self.emit(f'for (int {ix} = 0; {ix} < {w}; {ix}++) {{')
                    self.indent()
                    self.emit(f'if ({ix} > 0) printf(", ");')
                    self.emit(f'printf("{efmt}", {c_name}[{iy} * {w} + {ix}]);')
                    self.dedent(); self.emit('}'); self.emit('printf("]");')
                    self.dedent(); self.emit('}')
                    self.emit('printf("]\\n");')
                else:
                    ln = self.get_len_name(a1.id)
                    idx = self.fresh_var("__i_print")
                    self.emit(f'for (int {idx} = 0; {idx} < {ln}; {idx}++) {{')
                    self.indent(); self.emit(f'printf("{efmt}", {c_name}[{idx}]);')
                    self.emit(f'if ({idx} < {ln} - 1) printf(" ");'); self.dedent()
                    self.emit('}'); self.emit('printf("\\n");')
                return
            
            # Scalar case
            self.emit(f'printf("%s {fmt}\\n", "{prefix_c}", {val_str});')
            return

        # 2. 通用 Fallback 逻辑 (多参数)
        for idx_arg, a in enumerate(args):
            last = (idx_arg == len(args) - 1)
            if idx_arg > 0:
                self.emit('printf(" ");')
            
            fmt, val_str, is_arr = self._get_print_info(a)

            # 字符串常量格式化 (不对常量做 %s 嵌套以保持清晰)
            if isinstance(a, ast.Constant) and isinstance(a.value, str):
                escaped_value = self._escape_c_string(a.value)
                self.emit(f'printf("{escaped_value}");')
                if last: self.emit('printf("\\n");')
                continue

            # 数组打印
            if is_arr and isinstance(a, ast.Name):
                atype = self._get_expr_type(a)
                etype = atype.split("_arr")[0] if "_arr" in atype else "int"
                efmt = "%d"
                if etype == "double": efmt = "%2f"
                elif etype == "char *": efmt = "%s"
                elif "uint" in etype: efmt = "%u"
                
                c_name = self.get_renamed_param(a.id)
                if self.is_array2d(a.id):
                    h = self.get_len_name(a.id)
                    w = self.get_width_name(a.id)
                    iy = self.fresh_var("__y_print")
                    ix = self.fresh_var("__x_print")
                    self.emit('printf("[");')
                    self.emit(f'for (int {iy} = 0; {iy} < {h}; {iy}++) {{')
                    self.indent()
                    self.emit('if (%s > 0) printf(", ");' % iy)
                    self.emit('printf("[");')
                    self.emit(f'for (int {ix} = 0; {ix} < {w}; {ix}++) {{')
                    self.indent()
                    self.emit(f'if ({ix} > 0) printf(", ");')
                    self.emit(f'printf("{efmt}", {c_name}[{iy} * {w} + {ix}]);')
                    self.dedent(); self.emit('}'); self.emit('printf("]");')
                    self.dedent(); self.emit('}')
                    if last: self.emit('printf("]\\n");')
                    else: self.emit('printf("]");')
                else:
                    ln = self.get_len_name(a.id)
                    idx = self.fresh_var("__i_print")
                    self.emit('printf("[");')
                    self.emit(f'for (int {idx} = 0; {idx} < {ln}; {idx}++) {{')
                    self.indent(); self.emit(f'printf("{efmt}", {c_name}[{idx}]);')
                    self.emit(f'if ({idx} < {ln} - 1) printf(", ");'); self.dedent()
                    self.emit('}'); 
                    if last: self.emit('printf("]\\n");')
                    else: self.emit('printf("]");')
                continue

            else:
                # 标量打印
                if last:
                    self.emit(f'printf("{fmt}\\n", {val_str});')
                else:
                    self.emit(f'printf("{fmt}", {val_str});')
    def emit(self, line: str):
        """Emit a line of C code with proper indentation.
        
        Args:
            line: C code line to emit (without indentation prefix)
        """
        indent = self.indent_with * self.indent_level
        self.code.append(f"{indent}{line}")

    def _escape_c_string(self, s: str) -> str:
        """Escape a Python string for use in C string literals.
        
        Args:
            s: Python string to escape
            
        Returns:
            C-escaped string (e.g., \n, \t, \\ handled)
        """
        return (s.replace("\\", "\\\\")
                 .replace('"', '\\"')
                 .replace("\n", "\\n")
                 .replace("\r", "\\r")
                 .replace("\t", "\\t"))

    def indent(self):
        """Increase indentation level for subsequent emitted lines."""
        self.indent_level += 1

    def dedent(self):
        """Decrease indentation level for subsequent emitted lines."""
        self.indent_level -= 1

    # ----- entry -----
    def generate(self, tree: ast.AST) -> str:
        """Generate C code from a Python AST.
        
        Main entry point for transpilation. Processes the AST to:
        1. Extract #include directives from source comments
        2. Emit standard C headers (stdio.h, stdint.h, etc.)
        3. Visit all AST nodes to generate function and struct definitions
        4. Create a wrapper procedure for top-level statements
        5. Generate main() function that calls the wrapper
        
        Args:
            tree: Root AST node (typically ast.Module)
            
        Returns:
            Complete C source code as a string
        """
        include_lines: List[str] = []
        try:
            source = self.source_text
            if source is None:
                with open(self.filename, "r", encoding="utf-8", errors="ignore") as fp:
                    source = fp.read()
            for line in (source or "").splitlines():
                stripped = line.strip()
                if stripped.startswith("#include"):
                    include_lines.append(stripped)
        except Exception:
            include_lines = []
        for sub in ast.walk(tree):
            if isinstance(sub, ast.Subscript) and isinstance(sub.value, ast.Attribute):
                if (isinstance(sub.value.value, ast.Name)
                    and sub.value.value.id == "machine"
                    and sub.value.attr == "mem32"):
                    self.needs_mem32 = True
                    break
        for inc in include_lines:
            self.emit(inc)
        self.emit("#include <stdio.h>")
        self.emit("#include <stdint.h>")
        self.emit("#include <stdlib.h>")
        self.emit("#include <stdbool.h>") # 新增
        self.emit("#include <string.h>")  # 新增
        self.emit("#include <math.h>") # 新增
        self.emit("")
        if self.needs_mem32:
            self.emit("#define MEM32(addr) (*(volatile uint32_t*)(addr))")
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

    def _analyze_func_array2d_params(self, node: ast.FunctionDef) -> List[Tuple[str, str]]:
        arg_names = [a.arg for a in node.args.args]
        is_array2d = {name: False for name in arg_names}
        uses_width = {name: False for name in arg_names}

        for sub in ast.walk(node):
            if isinstance(sub, ast.Subscript) and isinstance(sub.value, ast.Subscript):
                inner = sub.value
                if isinstance(inner.value, ast.Name) and inner.value.id in is_array2d:
                    is_array2d[inner.value.id] = True
            if isinstance(sub, ast.Call) and isinstance(sub.func, ast.Name) and sub.func.id == "width":
                if sub.args and isinstance(sub.args[0], ast.Name) and sub.args[0].id in uses_width:
                    uses_width[sub.args[0].id] = True

        array2d_args = [a for a in arg_names if is_array2d[a] or uses_width[a]]
        arr2d_params: List[Tuple[str, str]] = []

        def pick_existing_width_name(arr_name: str) -> str | None:
            for cand in (f"w_{arr_name}", f"{arr_name}_w", "w", "width"):
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

        for a in array2d_args:
            existing = pick_existing_width_name(a)
            if existing:
                arr2d_params.append((a, existing))
            else:
                base = "w" if len(array2d_args) == 1 else f"w_{a}"
                arr2d_params.append((a, make_unique(base)))

        return arr2d_params

    # ----- visitors -----
    def _infer_return_type(self, fn: ast.FunctionDef) -> str:
        """
        Enhanced return-type inference for our Python subset.
        1. Support Python 3 type hints (e.g., def f() -> float:).
        2. Infer from return expressions and local variable assignments.
        """
        # 1. Check type annotations
        if fn.returns:
            return self._get_c_type_from_annotation(fn.returns)

        # 2. Semantic inference
        # Temporarily push a scope to track local variable types for better return inference
        self.push_scope()
        for stmt in fn.body:
            if isinstance(stmt, ast.Assign):
                for target in stmt.targets:
                    if isinstance(target, ast.Name):
                        try:
                            vtype = self._get_expr_type(stmt.value)
                            self.set_type_name(target.id, vtype)
                        except:
                            pass
            elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name):
                if isinstance(stmt.annotation, ast.Name):
                    self.set_type_name(stmt.target.id, stmt.annotation.id)

        return_types = set()
        has_return = False
        
        for sub in ast.walk(fn):
            if isinstance(sub, ast.Return):
                has_return = True
                if sub.value:
                    try:
                        rtype = self._get_expr_type(sub.value)
                        return_types.add(rtype)
                    except:
                        return_types.add("int")
                else:
                    return_types.add("void")
        
        self.pop_scope()

        if not has_return:
            return "void"
        
        # 3. Merge return types logic
        if "double" in return_types:
            return "double"
        if "int64_t" in return_types:
            return "int64_t"
        if "uint32_t" in return_types:
            return "uint32_t"
        if "uint16_t" in return_types:
            return "uint16_t"
        if "int" in return_types:
            return "int"
        if "bool" in return_types:
            return "bool"
        if "char *" in return_types:
            return "char *"
        
        # Check for struct types
        for t in return_types:
            if t in self.struct_names:
                return t
        
        if return_types == {"void"}:
            return "void"
        
        return "int"


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

    def _analyze_all_func_array2d_params(self, mod: ast.Module) -> None:
        func_defs: Dict[str, ast.FunctionDef] = {}
        func_args: Dict[str, List[str]] = {}
        for s in mod.body:
            if isinstance(s, ast.FunctionDef):
                func_defs[s.name] = s
                func_args[s.name] = [a.arg for a in s.args.args]

        arr2d_map: Dict[str, List[Tuple[str, str]]] = {}
        for name, fn in func_defs.items():
            arr2d_map[name] = self._analyze_func_array2d_params(fn)

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
                known_arr2d = {p for p, _ in arr2d_map.get(caller_name, [])}
                for sub in ast.walk(fn):
                    if isinstance(sub, ast.Call) and isinstance(sub.func, ast.Name):
                        callee = sub.func.id
                        if callee not in arr2d_map or not arr2d_map[callee]:
                            continue
                        for callee_arr_param, _callee_w in arr2d_map[callee]:
                            idx = param_index(callee, callee_arr_param)
                            if idx is None or idx >= len(sub.args):
                                continue
                            actual = sub.args[idx]
                            if isinstance(actual, ast.Name):
                                an = actual.id
                                if an in caller_params and an not in known_arr2d:
                                    width_name = None
                                    for cand in (f"{an}_w", "w", "width"):
                                        if cand in caller_params and cand != an:
                                            width_name = cand
                                            break
                                    if width_name is None:
                                        width_name = f"{an}_w"
                                    arr2d_map.setdefault(caller_name, []).append((an, width_name))
                                    known_arr2d.add(an)
                                    changed = True

        self.func_array2d_params = arr2d_map

    def _infer_field_types(self, class_node: ast.ClassDef, module_node: ast.AST) -> Dict[str, str]:
        """推导类中定义的字段（映射到 C 结构体）的类型。
        通过分析 __init__ 中的赋值及各处的实例化调用来推断成员变量应为 int 还是 double。
        """
        init_fn: Optional[ast.FunctionDef] = None
        for s in class_node.body:
            if isinstance(s, ast.FunctionDef) and s.name == "__init__":
                init_fn = s
                break
        
        if not init_fn:
            return {}

        field_types: Dict[str, str] = {}
        # 记录哪些字段是通过 __init__ 参数初始化的：param_name -> [field_names]
        param_to_fields: Dict[str, List[str]] = {}

        # 处理形参（跳过 self）
        params = [arg.arg for arg in init_fn.args.args[1:]]
            
        for s in init_fn.body:
            if isinstance(s, ast.Assign) and len(s.targets) == 1:
                t = s.targets[0]
                if isinstance(t, ast.Attribute) and isinstance(t.value, ast.Name) and t.value.id == "self":
                    field_name = t.attr
                    val = s.value
                    
                    # 情况 A: 直接赋值常量，如 self.integral = 0.0
                    if isinstance(val, ast.Constant):
                        field_types[field_name] = self._get_expr_type(val)
                    
                    # 情况 B: 赋值为形参，如 self.kp = kp
                    elif isinstance(val, ast.Name) and val.id in params:
                        # 检查 __init__ 中的类型注解
                        hinted_type = None
                        for arg in init_fn.args.args[1:]:
                            if arg.arg == val.id and arg.annotation:
                                hinted_type = self._get_c_type_from_annotation(arg.annotation)
                                break
                        
                        if hinted_type:
                            field_types[field_name] = hinted_type
                        else:
                            # 需要通过调用处（构造函数）来进一步推导
                            if val.id not in param_to_fields:
                                param_to_fields[val.id] = []
                            param_to_fields[val.id].append(field_name)
                    
                    # 情况 C: 其他简单表达式
                    else:
                        field_types[field_name] = self._get_expr_type(val)

        # 基于构造函数调用点（如 PIDController(1.5, ...)）补全字段类型推导
        if param_to_fields:
            class_name = class_node.name
            for node in ast.walk(module_node):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == class_name:
                    for i, arg in enumerate(node.args):
                        if i < len(params):
                            p_name = params[i]
                            if p_name in param_to_fields:
                                arg_type = self._get_expr_type(arg)
                                for f_name in param_to_fields[p_name]:
                                    field_types[f_name] = self._merge_types(field_types.get(f_name), arg_type)
        
        return field_types

    def visit_Module(self, node: ast.Module):
        struct_nodes: Set[int] = set()
        for i, s in enumerate(node.body):
            if isinstance(s, ast.ClassDef):
                field_names = self._parse_class_struct(s)
                type_map = self._infer_field_types(s, node)
                # 整合字段名与推导出的类型，默认 int
                fields = {fn: type_map.get(fn, "int") for fn in field_names}
                self.struct_defs[s.name] = fields
                self.struct_names.add(s.name)
                if s.name.startswith("GPIO_"):
                    self.external_struct_names.add(s.name)
                struct_nodes.add(i)
            if isinstance(s, ast.Assign):
                nt = self._parse_namedtuple_def(s)
                if nt:
                    name, field_list = nt
                    # 对于 namedtuple，默认字段均为 int
                    self.struct_defs[name] = {f: "int" for f in field_list}
                    self.struct_names.add(name)
                    struct_nodes.add(i)
        # Pre-analyze functions for array parameters (with propagation)
        self._analyze_all_func_array_params(node)
        self._analyze_all_func_array2d_params(node)
        # 1) 先把“可静态初始化”的顶层赋值提升为 C 全局变量
        #    这样能支持类似：state_box = [246813579, 0, 0] 这样的“全局数组”。
        lifted: Set[int] = set()
        for i, s in enumerate(node.body):
            if i in struct_nodes:
                continue
            if isinstance(s, ast.Assign) and self._is_static_global_assign(s):
                self._emit_global_assign(s)
                lifted.add(i)

        if lifted:
            self.emit("")

        # 2) 再输出所有顶层函数（C 不允许函数嵌套）
        funcs = [
            s for i, s in enumerate(node.body)
            if isinstance(s, ast.FunctionDef)
            and i not in struct_nodes
            and s.name not in ("byref", "extern")
            and not any(isinstance(d, ast.Name) and d.id == "extern" for d in s.decorator_list)
        ]
        # 预计算返回类型与 main 重命名映射
        for f in funcs:
            self.func_ret_types[f.name] = self._infer_return_type(f)
            if f.name == "main":
                self.func_name_alias["main"] = "user_main"

        # 先输出结构体定义
        if self.struct_defs:
            for name, fields in self.struct_defs.items():
                if name in self.external_struct_names:
                    continue
                self.emit("typedef struct {")
                self.indent()
                for f, t in fields.items():
                    self.emit(f"{t} {f};")
                self.dedent()
                self.emit(f"}} {name};")
                self.emit("")

        # 先输出函数原型，避免 C 的前置声明缺失
        for f in funcs:
            arr_params = self.func_array_params.get(f.name) or self._analyze_func_array_params(f)
            self.func_array_params[f.name] = arr_params
            arr2d_params = self.func_array2d_params.get(f.name) or self._analyze_func_array2d_params(f)
            self.func_array2d_params[f.name] = arr2d_params
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
                is_arr2d = any(p == arg.arg for p, _ in arr2d_params)
                
                if arg.annotation:
                    ptype = self._get_c_type_from_annotation(arg.annotation)
                    parts.append(f"{ptype} {c_param_name}")
                elif is_arr or is_arr2d:
                    parts.append(f"int *{c_param_name}")
                else:
                    struct_t = self._infer_struct_param_type(f, arg.arg)
                    if struct_t:
                        parts.append(f"{struct_t} {c_param_name}")
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

            for _, wn in arr2d_params:
                c_w_name = wn
                if wn in self.global_vars:
                    c_w_name = f"_arg_{wn}"
                    param_rename_map[wn] = c_w_name
                if c_w_name not in rendered_names:
                    parts.append(f"int {c_w_name}")
                    rendered_names.add(c_w_name)

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
            if i not in lifted and i not in struct_nodes and not isinstance(s, ast.FunctionDef)
        ]

    def visit_FunctionDef(self, node: ast.FunctionDef):
        if node.name in ("byref", "extern"):
            return
        if any(isinstance(d, ast.Name) and d.id == "extern" for d in node.decorator_list):
            return
        arr_params = self.func_array_params.get(node.name) or self._analyze_func_array_params(node)
        self.func_array_params[node.name] = arr_params
        arr2d_params = self.func_array2d_params.get(node.name) or self._analyze_func_array2d_params(node)
        self.func_array2d_params[node.name] = arr2d_params

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
            is_arr2d = any(p == arg.arg for p, _ in arr2d_params)
            
            if arg.annotation:
                ptype = self._get_c_type_from_annotation(arg.annotation)
                parts.append(f"{ptype} {c_param_name}")
            elif is_arr or is_arr2d:
                parts.append(f"int *{c_param_name}")
            else:
                struct_t = self._infer_struct_param_type(node, arg.arg)
                if struct_t:
                    parts.append(f"{struct_t} {c_param_name}")
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

        for orig_arr, wn in arr2d_params:
            c_w_name = wn
            if wn in self.global_vars:
                c_w_name = f"_arg_{wn}"
                param_rename_map[wn] = c_w_name
            if c_w_name not in rendered_names:
                parts.append(f"int {c_w_name}")
                rendered_names.add(c_w_name)

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
            if any(p == arg.arg for p, _ in arr2d_params):
                self.set_type_name(c_name, "int_arr2d")
            elif any(p == arg.arg for p, _ in arr_params):
                self.set_type_name(c_name, "int_arr")
            else:
                inferred = self._infer_struct_param_type(node, arg.arg)
                self.set_type_name(c_name, inferred or "int")

        # 参数视为已声明
        for arg in node.args.args:
            c_name = param_rename_map.get(arg.arg, arg.arg)
            self.vars.add(c_name)
            self.param_name_stack[-1].add(c_name)
        for _, ln in arr_params:
            c_len_name = param_rename_map.get(ln, ln)
            self.vars.add(c_len_name)
        for _, wn in arr2d_params:
            c_w_name = param_rename_map.get(wn, wn)
            self.vars.add(c_w_name)

        # len(arr) 在函数内替换为 n / n_<arg>
        # 关键：使用原始参数名作为键（因为Python代码中就是这样引用的）
        # 但值应该是C中实际的长度变量名（可能已重映射）
        for p, ln in arr_params:
            c_len_name = param_rename_map.get(ln, ln)
            # 注：这里保持 p（原始参数名）作为键，这样 len(arr) 中的 arr 可以正确查询
            self.set_len_name(p, c_len_name)
        for p, wn in arr2d_params:
            c_w_name = param_rename_map.get(wn, wn)
            self.set_width_name(p, c_w_name)
            self.mark_array2d(p)

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
                decl_type = self._decl_type(c_type)
                self.emit(f"{decl_type} {name};")
                self.vars.add(name)
                self.set_type_name(name, c_type)

        for stmt in node.body:
            self.visit(stmt)

        # 若函数体末尾已显式 return，则不再追加一个多余的 return;
        if not node.body or not isinstance(node.body[-1], ast.Return):
            if ret_type == "void":
                self.emit("return;")
            elif ret_type in self.struct_names:
                self.emit(f"return ({ret_type}){{0}};")
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
                if t in ("int_arr", "int_arr2d"):
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

    def visit_Import(self, node: ast.Import) -> None:
        """Skip import statements as dependencies are handled by C headers."""
        return

    def visit_ImportFrom(self, node: ast.ImportFrom) -> None:
        """Skip import-from statements."""
        return

    def visit_Attribute(self, node: ast.Attribute) -> str:
        if isinstance(node.value, ast.Name):
            if node.value.id == "math":
                if node.attr in MATH_CONST_MAP:
                    return MATH_CONST_MAP[node.attr]
                if node.attr in MATH_FUNC_MAP:
                    return MATH_FUNC_MAP[node.attr]
                return node.attr # For math functions not explicitly mapped, assume same name
            
            # Supports struct.member access
            base = node.value.id
            t = self.get_type_name(base)
            if t in self.struct_names:
                if node.attr not in self.struct_defs.get(t, []):
                    raise TranslateError(f"struct {t} 没有字段 {node.attr}", node)
                return f"{base}.{node.attr}"
        
        # machine.mem32[...] handled in visit_Subscript
        # Other attribute access not supported
        raise TranslateError(f"不支持的属性访问 (除结构体和 math 外): {ast.dump(node)}", node)

    def visit_Assign(self, node: ast.Assign):
        if len(node.targets) != 1:
            raise TranslateError("仅支持单目标赋值", node)
        
        target = node.targets[0]

        # 处理索引赋值（类似 a[i] = value）
        if isinstance(target, ast.Subscript):
            self.emit(f"{self.visit(target)} = {self.visit(node.value)};")
            return

        # 处理结构体字段赋值（类似 p.x = value）
        if isinstance(target, ast.Attribute) and isinstance(target.value, ast.Name):
            base = target.value.id
            t = self.get_type_name(base)
            if not t or t not in self.struct_names:
                raise TranslateError("属性赋值仅支持 struct 变量")
            if target.attr not in self.struct_defs.get(t, []):
                raise TranslateError(f"struct {t} 没有字段 {target.attr}", node)
            self.emit(f"{base}.{target.attr} = {self.visit(node.value)};")
            return

        # 处理普通变量赋值
        if not isinstance(target, ast.Name):
            raise TranslateError("不支持复杂赋值", node)

        var = target.id
        if self.in_wrapper_stack[-1] and self._is_external_macro_name(var):
            # Assume macro/constant defined in external headers.
            return
        val_node = node.value

        # 处理列表字面量赋值（例如 arr = [1, 2, 3]）
        if isinstance(val_node, ast.List):
            if var in self.global_vars and var not in self.vars:
                raise TranslateError(
                    f"全局数组 {var} 通过列表字面量初始化后，暂不支持在函数/局部作用域重新用 list 整体赋值（C 数组不可整体赋值）"
                )

            parsed_2d = self._parse_2d_list_literal(val_node)
            if parsed_2d:
                rows, cols, flat, elem_type = parsed_2d
                init = "{ " + ", ".join(flat) + " }"
                if var in self.vars:
                    raise TranslateError(f"变量 {var} 已声明，暂不支持再次整体赋 2D list")
                self.emit(f"{elem_type} {var}[] = {init};")
                self.vars.add(var)
                len_var = f"{var}_h"
                width_var = f"{var}_w"
                if len_var not in self.vars:
                    self.emit(f"int {len_var} = {rows};")
                    self.vars.add(len_var)
                else:
                    self.emit(f"{len_var} = {rows};")
                if width_var not in self.vars:
                    self.emit(f"int {width_var} = {cols};")
                    self.vars.add(width_var)
                else:
                    self.emit(f"{width_var} = {cols};")
                self.set_len_name(var, len_var)
                self.set_width_name(var, width_var)
                self.mark_array2d(var)
                self.set_type_name(var, f"{elem_type}_arr2d")
                self.set_type_name(len_var, "int")
                self.set_type_name(width_var, "int")
                return

            # 根据第一个元素推断一维数组类型，默认为 int
            elem_type = "int"
            if val_node.elts:
                elem_type = self._get_expr_type(val_node.elts[0])
            
            elems = [self.visit(e) for e in val_node.elts] 
            n = len(elems)
            init = "{ " + ", ".join(elems) + " }"

            # 检查是否已经声明过变量（避免重复声明）
            if var in self.vars:
                raise TranslateError(f"变量 {var} 已声明，暂不支持再次整体赋 list（C 数组不可整体赋值）")

            # 声明数组变量和初始化
            self.emit(f"{elem_type} {var}[] = {init};")
            self.vars.add(var)

            # 声明数组长度变量
            len_var = f"{var}_len"
            if len_var not in self.vars:
                self.emit(f"int {len_var} = {n};")
                self.vars.add(len_var)
            else:
                self.emit(f"{len_var} = {n};")

            self.set_len_name(var, len_var)
            self.set_type_name(var, f"{elem_type}_arr")
            self.set_type_name(len_var, "int")
            return

        if isinstance(val_node, ast.Call) and isinstance(val_node.func, ast.Name):
            ctor = val_node.func.id
            if ctor in self.struct_names:
                args = [self.visit(a) for a in val_node.args]
                init = f"({ctor}){{{', '.join(args)}}}"
                if var in self.vars:
                    self.emit(f"{var} = {init};")
                else:
                    self.emit(f"{ctor} {var} = {init};")
                    self.vars.add(var)
                self.set_type_name(var, ctor)
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
            decl_type = self._decl_type(c_type)
            self.emit(f"{decl_type} {var} = {rhs};")
            self.vars.add(var)
            self.set_type_name(var, c_type)
            return
        if var in explicit_globals and var not in self.global_vars:
            raise TranslateError(f"global 变量 {var} 未在顶层定义", node)

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

            # 如果推导出的类型是数组，在非 list 字面量赋值时应视为 int*
            decl_type = self._decl_type(c_type)

            # 如果变量已经在函数参数中声明，就不再重复声明
            if var not in self.vars:
                self.emit(f"{decl_type} {var} = {rhs};")
                self.vars.add(var)
                self.set_type_name(var, c_type)

    def visit_AugAssign(self, node: ast.AugAssign):
        target = self.visit(node.target)
        op = BINOP_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该增强赋值运算", node)
        if isinstance(node.target, ast.Name):
            var = node.target.id
            explicit_globals = self.explicit_global_stack[-1]
            if self.in_wrapper_stack[-1] and var in self.global_vars:
                val = self.visit(node.value)
                self.emit(f"{var} {op}= {val};")
                return
            if var in explicit_globals and var not in self.global_vars:
                raise TranslateError(f"global 变量 {var} 未在顶层定义", node)
            if var in self.global_vars and var not in self.vars and var not in explicit_globals:
                raise TranslateError(
                    f"变量 {var} 在函数内被赋值但未声明 global，Python 语义为局部变量"
                )
        val = self.visit(node.value)
        self.emit(f"{target} {op}= {val};")

    def visit_If(self, node: ast.If):
        # --- Handle if __name__ == "__main__": unwrap ---
        is_main_block = False
        if isinstance(node.test, ast.Compare):
            left = node.test.left
            if isinstance(left, ast.Name) and left.id == "__name__":
                if len(node.test.ops) == 1 and isinstance(node.test.ops[0], ast.Eq):
                    right = node.test.comparators[0]
                    if isinstance(right, ast.Constant) and right.value == "__main__":
                        is_main_block = True
                        
        if is_main_block:
            # Directly visit body statements instead of emitting a C if structure
            for s in node.body:
                self.visit(s)
            return

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
        # 1. 原有的 range(...) 逻辑
        if (isinstance(node.iter, ast.Call)
                and isinstance(node.iter.func, ast.Name)
                and node.iter.func.id == "range"):
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

        # 2. 新增：支持 for item in items:
        elif isinstance(node.iter, ast.Name):
            list_name = node.iter.id
            len_var = self.get_len_name(list_name)
            if not len_var:
                raise TranslateError(f"无法确定列表 '{list_name}' 的长度，请确保它是已实例化的列表或数组参数", node)
            
            # 获取列表元素的类型
            list_type = self.get_type_name(list_name, "int_arr")
            item_type = list_type.split("_arr")[0] if "_arr" in list_type else "int"
            if item_type == "int_arr2d": # Fail-safe for 2D flattening
                item_type = "int"
            
            # 生成一个唯一的索引变量名
            idx_var = self.fresh_var(f"__i_{list_name}")
            loop_var = node.target.id
            
            # 声明索引变量
            if idx_var not in self.vars:
                # self.emit(f"int {idx_var};") # Done in the loop header for modern C
                self.vars.add(idx_var)
            
            # 生成 C for 循环 (使用 C99 风格在 for 循环内声明 idx_var 以减少污染，或者保持前置声明)
            # 这里选择前置声明以兼容可能的严格 C89 目标，但当前库风格似乎倾向于混合
            self.emit(f"int {idx_var};")
            self.emit(f"for ({idx_var} = 0; {idx_var} < {len_var}; {idx_var}++) {{")
            self.indent()
            
            # 在循环体内部第一行：赋值当前元素给 Python 循环变量
            if loop_var not in self.vars:
                self.emit(f"{item_type} {loop_var} = {list_name}[{idx_var}];")
                self.vars.add(loop_var)
                self.set_type_name(loop_var, item_type)
            else:
                self.emit(f"{loop_var} = {list_name}[{idx_var}];")
            
            for s in node.body:
                self.visit(s)
            self.dedent()
            self.emit("}")

        else:
            raise TranslateError("仅支持 for x in range(...) 或 for x in list_name", node)



    def visit_While(self, node: ast.While):
        # Support: while <cond>:
        # (else: branch not supported)
        if node.orelse:
            raise TranslateError("暂不支持 while-else", node)
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

    def visit_Pass(self, node: ast.Pass):
        return

    # ----- expressions -----
    def visit_Call(self, node: ast.Call) -> str:
        if isinstance(node.func, ast.Name) and node.func.id == "byref":
            if len(node.args) != 1:
                raise TranslateError("byref 仅支持 1 个参数")
            return f"&({self.visit(node.args[0])})"
        if isinstance(node.func, ast.Name) and node.func.id in ("int", "float", "bool"):
            if len(node.args) != 1:
                raise TranslateError(f"{node.func.id} 仅支持 1 个参数")
            c_type = "int" if node.func.id == "int" else "double"
            if node.func.id == "bool":
                c_type = "bool"
            return f"(({c_type})({self.visit(node.args[0])}))"
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

        # abs(x)
        if isinstance(node.func, ast.Name) and node.func.id == "abs":
            if len(node.args) != 1:
                raise TranslateError("abs 仅支持 1 个参数")
            arg_type = self._get_expr_type(node.args[0])
            func = "fabs" if arg_type == "double" else "abs"
            return f"{func}({self.visit(node.args[0])})"

        # len(name)
        if isinstance(node.func, ast.Name) and node.func.id == "len":
            if len(node.args) != 1:
                raise TranslateError("len 仅支持 len(name) 或 len(arr[0])")
            arg0 = node.args[0]
            if isinstance(arg0, ast.Name):
                name = arg0.id
                ln = self.get_len_name(name)
                if not ln:
                    raise TranslateError(
                        f"len({name}) 无法解析长度变量（请先用 list 字面量初始化或传入数组形参）",
                        node
                    )
                return ln
            if isinstance(arg0, ast.Subscript) and isinstance(arg0.value, ast.Name):
                base = arg0.value.id
                if self.is_array2d(base):
                    wn = self.get_width_name(base)
                    if not wn:
                        raise TranslateError(f"len({base}[0]) 无法解析宽度变量")
                    return wn
            raise TranslateError("len 仅支持 len(name) 或 len(arr[0])")

        # width(name) for 2D array
        if isinstance(node.func, ast.Name) and node.func.id == "width":
            if len(node.args) != 1 or not isinstance(node.args[0], ast.Name):
                raise TranslateError("width 仅支持 width(name)")
            name = node.args[0].id
            wn = self.get_width_name(name)
            if not wn:
                raise TranslateError(f"width({name}) 无法解析宽度变量（仅支持 2D 数组）")
            return wn

        if isinstance(node.func, ast.Name) and node.func.id in self.struct_names:
            args = [self.visit(a) for a in node.args]
            return f"({node.func.id}){{{', '.join(args)}}}"

        if isinstance(node.func, ast.Name):
            func_name = self.func_name_alias.get(node.func.id, node.func.id)
        else:
            func_name = self.visit(node.func)
        # Auto-insert length/width args for known array parameters when missing.
        if isinstance(node.func, ast.Name):
            callee = node.func.id
            arr_params = self.func_array_params.get(callee, [])
            arr2d_params = self.func_array2d_params.get(callee, [])
            if arr_params or arr2d_params:
                fn_args = self.func_arg_names.get(callee, [])
                len_param_map = {ln: arr for arr, ln in arr_params}
                width_param_map = {wn: arr for arr, wn in arr2d_params}
                extra_param_map = dict(len_param_map)
                extra_param_map.update(width_param_map)
                extra_param_set = set(extra_param_map.keys())

                appended_extra: List[str] = []
                for _, ln in arr_params:
                    if ln not in fn_args and ln not in appended_extra:
                        appended_extra.append(ln)
                for _, wn in arr2d_params:
                    if wn not in fn_args and wn not in appended_extra:
                        appended_extra.append(wn)

                call_param_order = fn_args + appended_extra

                if len(node.args) < len(call_param_order):
                    call_args: List[str] = []
                    idx_in = 0
                    for i, param in enumerate(call_param_order):
                        remaining_args = len(node.args) - idx_in
                        remaining_non_extra = sum(
                            1 for p in call_param_order[i:] if p not in extra_param_set
                        )
                        if param in extra_param_set and remaining_args == remaining_non_extra:
                            arr_param = extra_param_map[param]
                            arr_idx = fn_args.index(arr_param) if arr_param in fn_args else None
                            if arr_idx is None or arr_idx >= len(node.args):
                                raise TranslateError(
                                    f"call {callee} missing array arg for extra '{param}'"
                                )
                            arr_arg = node.args[arr_idx]
                            extra_expr = None
                            if isinstance(arr_arg, ast.Name):
                                if param in len_param_map:
                                    extra_expr = self.get_len_name(arr_arg.id)
                                else:
                                    extra_expr = self.get_width_name(arr_arg.id)
                            if not extra_expr:
                                raise TranslateError(
                                    f"cannot resolve extra '{param}' in call {callee}()"
                                )
                            call_args.append(extra_expr)
                            continue

                        if idx_in < len(node.args):
                            call_args.append(self.visit(node.args[idx_in]))
                            idx_in += 1
                            continue

                        if param in extra_param_set:
                            arr_param = extra_param_map[param]
                            arr_idx = fn_args.index(arr_param) if arr_param in fn_args else None
                            if arr_idx is None or arr_idx >= len(node.args):
                                raise TranslateError(
                                    f"call {callee} missing array arg for extra '{param}'"
                                )
                            arr_arg = node.args[arr_idx]
                            extra_expr = None
                            if isinstance(arr_arg, ast.Name):
                                if param in len_param_map:
                                    extra_expr = self.get_len_name(arr_arg.id)
                                else:
                                    extra_expr = self.get_width_name(arr_arg.id)
                            if not extra_expr:
                                raise TranslateError(
                                    f"cannot resolve extra '{param}' in call {callee}()"
                                )
                            call_args.append(extra_expr)
                            continue

                        raise TranslateError(f"call {callee} missing required argument '{param}'")

                    return f"{func_name}({', '.join(call_args)})"

        args = [self.visit(a) for a in node.args]
        return f"{func_name}({', '.join(args)})"


    def visit_Compare(self, node: ast.Compare) -> str:
        if len(node.ops) != 1 or len(node.comparators) != 1:
            raise TranslateError("仅支持简单比较", node)
        
        lt = self._get_expr_type(node.left)
        rt = self._get_expr_type(node.comparators[0])
        
        if lt == "char *" and rt == "char *" and isinstance(node.ops[0], ast.Eq):
            return f"(strcmp({self.visit(node.left)}, {self.visit(node.comparators[0])}) == 0)"
        if lt == "char *" and rt == "char *" and isinstance(node.ops[0], ast.NotEq):
            return f"(strcmp({self.visit(node.left)}, {self.visit(node.comparators[0])}) != 0)"

        op = CMPOP_MAP.get(type(node.ops[0]))
        if not op:
            raise TranslateError("不支持该比较运算", node)
        return f"({self.visit(node.left)} {op} {self.visit(node.comparators[0])})"

    
    def visit_BinOp(self, node: ast.BinOp) -> str:
        if isinstance(node.op, ast.Add):
            lt = self._get_expr_type(node.left)
            rt = self._get_expr_type(node.right)
            if lt == "char *" or rt == "char *":
                raise TranslateError(
                    "string concat disabled for embedded targets; use const strings or format manually",
                    node
                )
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

        if isinstance(node.op, ast.Pow):
            return f"pow((double)({self.visit(node.left)}), (double)({self.visit(node.right)}))"

        op = BINOP_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该二元运算", node)
        return f"({self.visit(node.left)} {op} {self.visit(node.right)})"

    def visit_UnaryOp(self, node: ast.UnaryOp) -> str:
        op = UNARY_MAP.get(type(node.op))
        if not op:
            raise TranslateError("不支持该一元运算", node)
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
        if isinstance(node.value, ast.Attribute):
            if (isinstance(node.value.value, ast.Name)
                and node.value.value.id == "machine"
                and node.value.attr == "mem32"):
                self.needs_mem32 = True
                addr = self.visit(node.slice)
                return f"MEM32({addr})"
        if isinstance(node.value, ast.Subscript):
            inner = node.value
            if not isinstance(inner.value, ast.Name):
                raise TranslateError("仅支持 name[y][x]")
            base_name = inner.value.id
            if not self.is_array2d(base_name):
                raise TranslateError("2D 下标仅支持 2D 数组变量")
            width = self.get_width_name(base_name)
            if not width:
                raise TranslateError(f"2D array {base_name} missing width metadata")
            base = self.get_renamed_param(base_name)
            y = self.visit(inner.slice)
            x = self.visit(node.slice)
            return f"{base}[({y}) * ({width}) + ({x})]"
        if isinstance(node.value, ast.Name):
            base_name = node.value.id
            if self.is_array2d(base_name):
                raise TranslateError("2D 数组请使用 arr[y][x]")
            base = self.get_renamed_param(base_name)  # 应用参数重映射
            idx = self.visit(node.slice)
            return f"{base}[{idx}]"
        raise TranslateError("仅支持 name[index] 或 name[y][x]")

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
        if isinstance(node.value, int):
            val = node.value
            suffix = ""
            if val > 4294967295 or val < -2147483648:
                suffix = "LL"
            elif val > 2147483647:
                suffix = "U"
            
            if self._is_hex_literal(node) or self._is_octal_literal(node):
                segment = self._get_source_segment(node)
                if segment:
                    return self._normalize_int_literal(segment) + suffix
            return str(val) + suffix
        return str(node.value)

    def generic_visit(self, node):
        """Handle unsupported AST nodes by raising an error.
        
        Args:
            node: Unsupported AST node
            
        Raises:
            TranslateError: Always raised for unsupported syntax
        """
        raise TranslateError(f"不支持的语法节点: {type(node).__name__}（{self.filename}:{getattr(node,'lineno','?')}）")
    
    def generate(self, tree: ast.Module, stats: Optional[TranspilationStats] = None) -> str:
        """Generate C code from Python AST with enhanced headers and statistics.
        
        Args:
            tree: Python AST Module node
            stats: Optional statistics tracker to record metrics
            
        Returns:
            Complete C source code as a string
        """
        if stats:
            stats.start()
            if self.source_text:
                stats.python_lines = len(self.source_text.splitlines())
        
        # Generate C code header with metadata
        header_lines = []
        header_lines.append("/*")
        header_lines.append(f" * Generated by pdePy2c v{VERSION}")
        header_lines.append(f" * Source: {os.path.basename(self.filename)}")
        header_lines.append(f" * Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        header_lines.append(" * ")
        header_lines.append(" * This file was automatically transpiled from Python to C.")
        header_lines.append(" * Manual modifications may be overwritten on next transpilation.")
        header_lines.append(" */")
        header_lines.append("")
        
        # Add standard includes
        header_lines.append("#include <stdio.h>")
        header_lines.append("#include <stdlib.h>")
        header_lines.append("#include <stdint.h>")
        header_lines.append("#include <stdbool.h>")
        header_lines.append("#include <string.h>")
        header_lines.append("#include <math.h>")
        header_lines.append("")
        
        # Visit the module to generate code
        self.visit(tree)
        
        # Add wrapper for top-level statements if any
        if self._top_level_stmts:
            self.push_scope()
            self.in_wrapper_stack[-1] = True
            self.emit("void _toplevel_wrapper() {")
            self.indent()
            for s in self._top_level_stmts:
                self.visit(s)
            self.dedent()
            self.emit("}")
            self.emit("")
            self.pop_scope()
        
        # Add helper functions if needed
        helper_lines = []
        if self.needs_str_concat:
            helper_lines.append("char* str_concat(const char* a, const char* b) {")
            helper_lines.append("    size_t la = strlen(a), lb = strlen(b);")
            helper_lines.append("    char* r = (char*)malloc(la + lb + 1);")
            helper_lines.append("    memcpy(r, a, la);")
            helper_lines.append("    memcpy(r + la, b, lb + 1);")
            helper_lines.append("    return r;")
            helper_lines.append("}")
            helper_lines.append("")
        
        if self.needs_mem32:
            helper_lines.append("#define MEM32(addr) (*((volatile uint32_t*)(addr)))")
            helper_lines.append("")
        
        # Add automatic main() function
        main_lines = []
        has_user_main = "main" in self.func_name_alias  # Check if Python had a main function
        has_toplevel = bool(self._top_level_stmts)
        
        main_lines.append("// Auto-generated main function")
        main_lines.append("int main() {")
        
        if has_toplevel:
            main_lines.append("    _toplevel_wrapper();")
        
        if has_user_main:
            main_lines.append("    return user_main();")
        else:
            main_lines.append("    return 0;")
        
        main_lines.append("}")
        main_lines.append("")
        
        # Assemble final code
        final_code = "\n".join(header_lines + helper_lines + self.code + main_lines)
        
        # Collect statistics
        if stats:
            stats.c_lines = len(final_code.splitlines())
            stats.end()
            
            # Count functions and classes
            for line in self.code:
                if line.strip().startswith("typedef struct"):
                    stats.num_classes += 1
            
            stats.num_functions = len([f for f in self.func_ret_types.keys()])
            stats.num_global_vars = len(self.global_vars)
            
            # Record type usage
            for type_dict in self.type_stack:
                for var, type_name in type_dict.items():
                    stats.record_type(type_name)
        
        return final_code


def collect_pyfiles(paths: List[str]) -> List[str]:
    """Collect all Python files from given paths (files or directories).
    
    Recursively walks directories to find .py files.
    
    Args:
        paths: List of file paths or directory paths
        
    Returns:
        Sorted list of unique .py file paths
    """
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
    """Check Python syntax for all files and write results to output file.
    
    Uses py_compile to validate Python syntax before transpilation.
    
    Args:\n        pyfiles: List of Python file paths to check
        out_path: Path to write syntax check results
        
    Returns:
        List of files that passed syntax check
    """
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

def translate_files(pyfiles: List[str], out_dir: str, out_path: str, verbose: bool = False, show_stats: bool = False) -> None:
    """Transpile Python files to C and write results to output directory.
    
    For each Python file:
    1. Parse the Python source into an AST
    2. Generate C code using CCodeGenerator
    3. Write C code to output directory with pyc_ prefix
    4. Log success/failure with detailed error information
    
    Args:
        pyfiles: List of Python file paths to transpile
        out_dir: Directory to write generated C files
        out_path: Path to write transpilation results log
        verbose: Enable verbose output
        show_stats: Show detailed statistics
    """
    import traceback
    os.makedirs(out_dir, exist_ok=True)
    ok = 0
    bad = 0
    lines: List[str] = []
    all_stats = []
    
    # Add header with timestamp
    lines.append(f"pdePy2c v{VERSION} - 转换报告")
    lines.append(f"生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"输出目录: {out_dir}")
    lines.append("=" * 60)
    lines.append("")
    
    for i, f in enumerate(pyfiles, 1):
        if verbose:
            print(f"[{i}/{len(pyfiles)}] 转换: {f}...", end=" ")
        
        try:
            src = open(f, "r", encoding="utf-8").read()
            tree = ast.parse(src, filename=f)
            
            stats = TranspilationStats() if show_stats else None
            c_code = CCodeGenerator(filename=f, source_text=src).generate(tree, stats=stats)
            
            base = os.path.splitext(os.path.basename(f))[0]
            c_file = os.path.join(out_dir, f"pyc_{base}.c")
            _force_write_text(c_file, c_code)
            ok += 1
            lines.append(f"✅ 转换成功: {f} -> {c_file}")
            
            if stats:
                all_stats.append((f, stats))
                if verbose:
                    print(f"✓ ({stats.python_lines} 行 Python -> {stats.c_lines} 行 C, {stats.elapsed_time():.2f}s)")
            elif verbose:
                print("✓")
                
        except TranslateError as e:
            bad += 1
            lines.append(f"❌ 转换失败: {f}")
            lines.append(f"错误类型: TranslateError")
            lines.append(f"错误信息: {str(e)}")
            
            if verbose:
                print(f"✗ {str(e)}")
            
            # 如果异常包含行号信息，显示出错位置
            if hasattr(e, 'lineno') and e.lineno:
                lines.append(f"出错位置: 第 {e.lineno} 行")
                try:
                    src_lines = src.split('\n')
                    if 0 < e.lineno <= len(src_lines):
                        # 显示上下文（前后各2行）
                        start = max(1, e.lineno - 2)
                        end = min(len(src_lines), e.lineno + 2)
                        lines.append("代码上下文:")
                        for i in range(start, end + 1):
                            if i == e.lineno:
                                lines.append(f"→ {i:4d}: {src_lines[i - 1].rstrip()}")
                            else:
                                lines.append(f"  {i:4d}: {src_lines[i - 1].rstrip()}")
                except:
                    pass
            lines.append("")
        except SyntaxError as e:
            bad += 1
            lines.append(f"❌ 转换失败: {f}")
            lines.append(f"错误类型: Python语法错误")
            lines.append(f"错误信息: {e.msg}")
            if verbose:
                print(f"✗ Python语法错误: {e.msg}")
            if e.lineno:
                lines.append(f"出错位置: 第 {e.lineno} 行, 第 {e.offset} 列")
                if e.text:
                    lines.append(f"代码内容: {e.text.rstrip()}")
                    if e.offset:
                        lines.append(" " * (e.offset - 1) + "^")
            lines.append("")
        except Exception as e:
            bad += 1
            lines.append(f"❌ 转换失败: {f}")
            lines.append(f"错误类型: {type(e).__name__}")
            lines.append(f"错误信息: {str(e)}")
            if verbose:
                print(f"✗ {type(e).__name__}: {str(e)}")
            # 显示完整的堆栈跟踪
            lines.append("详细堆栈:")
            lines.append(traceback.format_exc())
            lines.append("")
    
    lines.append("")
    lines.append("=" * 60)
    lines.append(f"汇总: 成功 {ok} 个, 失败 {bad} 个")
    
    # Add aggregate statistics if requested
    if show_stats and all_stats:
        lines.append("")
        lines.append("=" * 60)
        lines.append("总体统计信息")
        lines.append("=" * 60)
        total_py_lines = sum(s.python_lines for _, s in all_stats)
        total_c_lines = sum(s.c_lines for _, s in all_stats)
        total_time = sum(s.elapsed_time() for _, s in all_stats)
        lines.append(f"总 Python 代码行数: {total_py_lines}")
        lines.append(f"总生成 C 代码行数: {total_c_lines}")
        if total_py_lines > 0:
            lines.append(f"平均代码膨胀率: {total_c_lines/total_py_lines:.2f}x")
        lines.append(f"总转换耗时: {total_time:.3f} 秒")
        if len(all_stats) > 0:
            lines.append(f"平均每文件耗时: {total_time/len(all_stats):.3f} 秒")
    
    lines.append("=" * 60)
    _force_write_text(out_path, "\n".join(lines))
    
    if verbose:
        print()
        print(f"转换完成: 成功 {ok} 个, 失败 {bad} 个")
        print(f"详细报告已写入: {out_path}")


def main():
    """Main entry point for the pdePy2c transpiler command-line tool.
    
    Workflow:
    1. Parse command-line arguments for input Python files/directories
    2. Collect all .py files from specified paths
    3. Run syntax check on collected files
    4. Transpile syntax-valid files to C code
    5. Write results to result_1.txt (syntax check) and result_2.txt (transpilation)
    """
    parser = argparse.ArgumentParser(
        description=f"pdePy2c v{VERSION} - Python to C Transpiler",
        epilog="Transpiles a restricted Python subset to C code with automatic type inference."
    )
    parser.add_argument("pyfiles", nargs="*", default=["./PyFiles"], 
                        help="Python files/directories (default: ./PyFiles)")
    parser.add_argument("-o", "--output-dir", default="PythonC", 
                        help="Output directory for generated C files (default: PythonC)")
    parser.add_argument("-v", "--verbose", action="store_true", 
                        help="Enable verbose output showing transpilation progress")
    parser.add_argument("--stats", action="store_true", 
                        help="Show detailed statistics after transpilation")
    parser.add_argument("--version", action="version", version=f"pdePy2c {VERSION}")
    parser.add_argument("-s", "--single-file", action="store_true", 
                        help="Transpile single file without directory checks")
    parser.add_argument("--dry-run", action="store_true", 
                        help="Parse and analyze files without generating output")
    args = parser.parse_args()

    # Skip directory check if single-file mode or if user specified custom paths
    default_dir = "./PyFiles"
    if not args.single_file and args.pyfiles == [default_dir]:
        if not os.path.exists(default_dir):
            print(f"错误：默认目录 '{default_dir}' 不存在！请先创建该文件夹并放入.py文件", file=sys.stderr)
            print(f"提示：使用 -s 选项可以转换单个文件，或直接指定文件/目录路径", file=sys.stderr)
            sys.exit(1)

    if args.verbose:
        print(f"pdePy2c v{VERSION} - Python to C Transpiler")
        print(f"输出目录: {args.output_dir}")
        print(f"模式: {'试运行' if args.dry_run else '正常转换'}")
        print()

    pyfiles = collect_pyfiles(args.pyfiles)
    if not pyfiles:
        print("未找到 .py 文件。", file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        print(f"找到 {len(pyfiles)} 个 Python 文件")
        print()

    ok_files = syntax_check(pyfiles, "result_1.txt")
    
    if args.dry_run:
        print(f"试运行模式：将转换 {len(ok_files)} 个文件（不生成输出）")
        for f in ok_files:
            print(f"  - {f}")
    else:
        translate_files(ok_files, args.output_dir, "result_2.txt", 
                       verbose=args.verbose, show_stats=args.stats)

if __name__ == "__main__":
    main()
