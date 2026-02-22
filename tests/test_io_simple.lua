-- Simple IO test
local filename = "test.txt"
local mode = "w"
local file = io_open(filename, mode)
print(file)
