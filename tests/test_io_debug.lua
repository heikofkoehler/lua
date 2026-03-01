-- Debug test
print("Test 1")
local file = io.open("test.txt", "w")
file:write("ABC")
file:close()

print("Test 2")
local file2 = io.open("test.txt", "r")
local content = file2:read()
file2:close()

print("Test 3")
