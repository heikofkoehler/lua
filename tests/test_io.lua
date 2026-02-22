-- Test file I/O functions

-- Test 1: Write to file
print("Writing to file...")
local file = io_open("test_output.txt", "w")
io_write(file, "Hello from Lua!\n")
io_write(file, "Line 2\n")
io_write(file, "Line 3\n")
io_close(file)
print("Write complete")

-- Test 2: Read from file
print("Reading from file...")
local file2 = io_open("test_output.txt", "r")
local content = io_read(file2)
print(content)
io_close(file2)
print("Read complete")
