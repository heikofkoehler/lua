-- Test nested function calls
print("Simple test:")
print("hello")

print("Reverse test:")
local rev = string.reverse("hello")
print(rev)

print("Upper test:")
local up = string.upper("hello")
print(up)

print("Nested test:")
print(string.upper(string.reverse("hello")))

print("Done")
