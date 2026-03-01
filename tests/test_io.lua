-- Test file I/O functions

-- Test 1: Write to file
print("Writing to file...")
local file = io.open("test_output.txt", "w")
file:write("Hello from Lua!\n")
file:write("Line 2\n")
file:write("Line 3\n")
file:close()
print("Write complete")

-- Test 2: Read from file
print("Reading from file...")
local file2 = io.open("test_output.txt", "r")
local content = file2:read()
print(content)
file2:close()
print("Read complete")
