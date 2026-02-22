-- Debug test
print("Test 1")
local file = io_open("test.txt", "w")
io_write(file, "ABC")
io_close(file)

print("Test 2")
local file2 = io_open("test.txt", "r")
local content = io_read(file2)
io_close(file2)

print("Test 3")
