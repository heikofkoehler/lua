-- Comprehensive Standard Library Demo
print("=== Lua VM Standard Library Demo ===")

-- String Library
print("")
print("STRING LIBRARY:")
print("Length of 'hello':")
local len_fn = string["len"]
print(len_fn("hello"))

print("Uppercase 'hello':")
local upper_fn = string["upper"]
print(upper_fn("hello"))

print("Lowercase 'WORLD':")
local lower_fn = string["lower"]
print(lower_fn("WORLD"))

print("Reverse 'Lua':")
local reverse_fn = string["reverse"]
print(reverse_fn("Lua"))

print("Substring of 'hello' (2, 4):")
local sub_fn = string["sub"]
print(sub_fn("hello", 2, 4))

print("Byte value of 'A':")
local byte_fn = string["byte"]
print(byte_fn("A"))

print("Char from bytes 72, 105:")
local char_fn = string["char"]
print(char_fn(72, 105))

-- Table Library
print("")
print("TABLE LIBRARY:")
local numbers = {10, 20, 30}
local insert_fn = table["insert"]
insert_fn(numbers, 40)
print("After inserting 40:")
print(numbers[4])

insert_fn(numbers, 2, 15)
print("After inserting 15 at position 2:")
print(numbers[2])

local remove_fn = table["remove"]
local removed = remove_fn(numbers, 1)
print("Removed value:")
print(removed)

local items = {"apple", "banana", "cherry"}
local concat_fn = table["concat"]
local joined = concat_fn(items, ", ")
print("Concatenated items:")
print(joined)

-- Math Library
print("")
print("MATH LIBRARY:")

print("sqrt(16):")
local sqrt_fn = math["sqrt"]
print(sqrt_fn(16))

print("abs(-42):")
local abs_fn = math["abs"]
print(abs_fn(-42))

print("floor(3.7):")
local floor_fn = math["floor"]
print(floor_fn(3.7))

print("ceil(3.2):")
local ceil_fn = math["ceil"]
print(ceil_fn(3.2))

print("sin(0):")
local sin_fn = math["sin"]
print(sin_fn(0))

print("cos(0):")
local cos_fn = math["cos"]
print(cos_fn(0))

print("exp(2):")
local exp_fn = math["exp"]
print(exp_fn(2))

print("log(2.718281828459):")
local log_fn = math["log"]
print(log_fn(2.718281828459))

print("min(5, 2, 8, 1):")
local min_fn = math["min"]
print(min_fn(5, 2, 8, 1))

print("max(5, 2, 8, 1):")
local max_fn = math["max"]
print(max_fn(5, 2, 8, 1))

print("pi constant:")
local pi = math["pi"]
print(pi)

print("")
print("=== Demo Complete ===")
