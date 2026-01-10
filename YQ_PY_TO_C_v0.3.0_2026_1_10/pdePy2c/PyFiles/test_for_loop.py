def test_for_loop():
    print("--- Test: for item in list ---")
    items = [10, 20, 30, 40, 50]
    for item in items:
        print("Item:", item)

    print("\n--- Test: for val in double_list ---")
    prices = [1.99, 2.50, 3.75]
    total = 0.0
    for p in prices:
        print("Price:", p)
        total += p
    print("Total:", total)

test_for_loop()
