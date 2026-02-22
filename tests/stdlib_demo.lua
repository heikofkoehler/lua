-- Comprehensive Standard Library Demo
print("=== Lua VM Standard Library Demo ===")

-- String Library
print("")
print("STRING LIBRARY:")
print("string.len('hello') = " .. string["len"]("hello"))
print("string.upper('hello') = " .. string["upper"]("hello"))
print("string.lower('WORLD') = " .. string["lower"]("WORLD"))
print("string.reverse('Lua') = " .. string["reverse"]("Lua"))
print("string.sub('hello', 2, 4) = " .. string["sub"]("hello", 2, 4))

-- String byte/char
local byte_val = string["byte"]("A")
print("string.byte('A') = " .. byte_val)
local char_result = string["char"](72, 105)
print("string.char(72, 105) = " .. char_result)

-- Table Library
print("")
print("TABLE LIBRARY:")
local numbers = {10, 20, 30}
table["insert"](numbers, 40)
print("After table.insert(numbers, 40):")
print("  numbers[4] = " .. numbers[4])

table["insert"](numbers, 2, 15)
print("After table.insert(numbers, 2, 15):")
print("  numbers[2] = " .. numbers[2])

local removed = table["remove"](numbers, 1)
print("table.remove(numbers, 1) returned: " .. removed)

local items = {"apple", "banana", "cherry"}
local joined = table["concat"](items, ", ")
print("table.concat({'apple','banana','cherry'}, ', '):")
print("  " .. joined)

-- Math Library
print("")
print("MATH LIBRARY:")
print("math.sqrt(16) = " .. math["sqrt"](16))
print("math.abs(-42) = " .. math["abs"](-42))
print("math.floor(3.7) = " .. math["floor"](3.7))
print("math.ceil(3.2) = " .. math["ceil"](3.2))

print("Trigonometry:")
print("  math.sin(0) = " .. math["sin"](0))
print("  math.cos(0) = " .. math["cos"](0))

print("math.exp(2) = " .. math["exp"](2))
print("math.log(2.718281828459) = " .. math["log"](2.718281828459))

print("math.min(5, 2, 8, 1) = " .. math["min"](5, 2, 8, 1))
print("math.max(5, 2, 8, 1) = " .. math["max"](5, 2, 8, 1))

print("math.pi = " .. math["pi"])

print("")
print("=== Demo Complete ===")
