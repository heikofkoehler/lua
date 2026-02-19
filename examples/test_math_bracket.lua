print("=== Math Library Tests ===")

-- Basic functions
print("Basic math functions:")
local sqrt = math["sqrt"]
local abs = math["abs"]
local floor = math["floor"]
local ceil = math["ceil"]
print(sqrt(16))  -- 4
print(abs(-5))  -- 5
print(floor(3.7))  -- 3
print(ceil(3.2))  -- 4

-- Trigonometry
print("Trigonometry:")
local sin = math["sin"]
local cos = math["cos"]
local tan = math["tan"]
print(sin(0))  -- 0
print(cos(0))  -- 1
print(tan(0))  -- 0

-- Logarithms
print("Logarithms:")
local exp = math["exp"]
local log = math["log"]
print(exp(1))  -- 2.71828...
print(log(2.71828))  -- ~1

-- Min/max
print("Min/max:")
local min = math["min"]
local max = math["max"]
print(min(5, 2, 8, 1))  -- 1
print(max(5, 2, 8, 1))  -- 8

-- Constants
print("Constants:")
local pi = math["pi"]
print(pi)  -- 3.14159...
