-- Comprehensive IO test

print("=== File I/O Test ===")

-- Test 1: Write to file
print("Test 1: Writing to demo.txt")
local outfile = io_open("demo.txt", "w")
io_write(outfile, "Line 1")
io_write(outfile, " ")
io_write(outfile, "Line 2")
io_close(outfile)
print("Write complete")

-- Test 2: Read from file
print("Test 2: Reading from demo.txt")
local infile = io_open("demo.txt", "r")
local content = io_read(infile)
io_close(infile)
print("File contents:")
print(content)

-- Test 3: Append to file
print("Test 3: Appending to demo.txt")
local appendfile = io_open("demo.txt", "a")
io_write(appendfile, " Line 3")
io_close(appendfile)

-- Test 4: Read again to see appended content
print("Test 4: Reading updated file")
local readfile = io_open("demo.txt", "r")
local newcontent = io_read(readfile)
io_close(readfile)
print("Updated contents:")
print(newcontent)

print("=== All tests complete ===")
