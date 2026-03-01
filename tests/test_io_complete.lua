-- Comprehensive IO test

print("=== File I/O Test ===")

-- Test 1: Write to file
print("Test 1: Writing to demo.txt")
local outfile = io.open("demo.txt", "w")
outfile:write("Line 1")
outfile:write(" ")
outfile:write("Line 2")
outfile:close()
print("Write complete")

-- Test 2: Read from file
print("Test 2: Reading from demo.txt")
local infile = io.open("demo.txt", "r")
local content = infile:read()
infile:close()
print("File contents:")
print(content)

-- Test 3: Append to file
print("Test 3: Appending to demo.txt")
local appendfile = io.open("demo.txt", "a")
appendfile:write(" Line 3")
appendfile:close()

-- Test 4: Read again to see appended content
print("Test 4: Reading updated file")
local readfile = io.open("demo.txt", "r")
local newcontent = readfile:read()
readfile:close()
print("Updated contents:")
print(newcontent)

print("=== All tests complete ===")
