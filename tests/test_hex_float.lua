-- Test hexadecimal floats in string.format
print("Testing hexadecimal floats (%a, %A)...")

local f1 = string.format("%a", 1.0)
print("1.0 in hex: " .. f1)
assert(string.find(f1, "0x1"))

local f2 = string.format("%a", 0.5)
print("0.5 in hex: " .. f2)
assert(string.find(f2, "0x[01]."))

local f3 = string.format("%A", 10.0)
print("10.0 in hex: " .. f3)
assert(string.find(f3, "0X"))

print("OK: hexadecimal float tests passed")
