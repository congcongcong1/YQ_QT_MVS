# test_types.py
LARGE_VAL = 4294967296  # Should be int64_t
ADDR = 0x40010800       # Should be uint32_t
HEX_LIST = [0x10, 0x20, 0x30] # Should be hex in C

def test():
    x = 0xFFFFFFFF
    print(x)
