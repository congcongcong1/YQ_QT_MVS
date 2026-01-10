def add(a: int, b: int) -> int:
    return a + b

def get_pi() -> float:
    return 3.14159

def is_positive(n: int) -> bool:
    if n > 0:
        return True
    return False

def multiply(a: float, b: float) -> float:
    return a * b

print(add(1, 2))
print(get_pi())
print(is_positive(10))
print(multiply(2.5, 4.0))
