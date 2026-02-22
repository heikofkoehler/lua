-- Debug test
print("Getting len function")
local len = string["len"]
print("Got len function")

print("Getting upper function")
local upper = string["upper"]
print("Got upper function")

print("Calling len('hello')")
local result1 = len("hello")
print("Result from len:")
print(result1)

print("Calling upper('hello')")
local result2 = upper("hello")
print("Result from upper:")
print(result2)
