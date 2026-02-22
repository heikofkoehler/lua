-- Debug stack issues
print("Test 1:")
print(string.reverse("abc"))

print("Test 2:")
local x = string.reverse("abc")
print(x)

print("Test 3:")
local y = string.upper(x)
print(y)

print("Test 4 - direct nested:")
print(string.upper("abc"))
