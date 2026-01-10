# test_math.py
import math

def test_math():
    x = 1.0
    s = math.sin(x)
    c = math.cos(x)
    t = math.tan(x)
    sq = math.sqrt(2.0)
    p = math.pow(2.0, 3.0)
    pi = math.pi
    
    print("sin(1.0) =", s)
    print("cos(1.0) =", c)
    print("sqrt(2.0) =", sq)
    print("pow(2,3) =", p)
    print("pi =", pi)
    
    # Test abs
    a = -5
    fa = -5.5
    print("abs(-5) =", abs(a))
    print("abs(-5.5) =", abs(fa))


test_math()