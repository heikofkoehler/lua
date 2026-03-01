-- Simple IO test
local filename = "test.txt"
local mode = "w"
local file = io.open(filename, mode)
print(file)
