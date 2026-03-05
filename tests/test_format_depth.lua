-- Test string.format depth
print("Testing string.format precision and padding...")

-- 1. Numeric padding and precision
assert(string.format("%05d", 42) == "00042")
assert(string.format("%+d", 42) == "+42")
assert(string.format("% d", 42) == " 42")
assert(string.format("%#x", 42) == "0x2a")

-- 2. Floating point precision
assert(string.format("%.2f", 1.2345) == "1.23")
assert(string.format("%10.2f", 1.2345) == "      1.23")
assert(string.format("%-10.2f", 1.2345) == "1.23      ")

-- 3. String width
assert(string.format("%5s", "hi") == "   hi")
assert(string.format("%.3s", "hello") == "hel")

-- 4. Multi-specifier
assert(string.format("%02d/%02d/%04d", 4, 3, 2026) == "04/03/2026")

print("OK: string.format depth tests passed")
