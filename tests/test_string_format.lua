-- Test improved string.format

print("Testing string.format basic...")
assert(string.format("%s", "hello") == "hello")
assert(string.format("%%") == "%")
print("OK")

print("Testing string.format integers...")
assert(string.format("%d", 123) == "123")
assert(string.format("%x", 255) == "ff")
assert(string.format("%X", 255) == "FF")
assert(string.format("%04d", 42) == "0042")
assert(string.format("%+d", 42) == "+42")
print("OK")

print("Testing string.format floats...")
-- Note: exact output might depend on platform snprintf, but basic checks should pass
assert(string.format("%.2f", 1.234) == "1.23")
assert(string.format("%e", 1000) == "1.000000e+03")
print("OK")

print("Testing string.format q...")
assert(string.format("%q", "a\nb") == '"a\\nb"')
assert(string.format("%q", "a\"b") == '"a\\"b"')
print("OK")

print("Testing string.format mixed...")
assert(string.format("%s = %d", "age", 25) == "age = 25")
print("OK")

print("All string.format tests passed!")
