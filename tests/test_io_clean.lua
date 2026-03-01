-- Clean IO test

-- Write to file
print("Writing to output.txt...")
local outfile = io.open("output.txt", "w")
outfile:write("Hello from Lua!")
outfile:write(" ")
outfile:write("This is line 2")
outfile:flush()
outfile:close()
print("Done writing")

-- Read from file
print("Reading from output.txt...")
local infile = io.open("output.txt", "r")
local data = infile:read()
infile:seek("set", 0)
local data2 = infile:read()
infile:close()

-- Print what we read
print("File contents:")
print(data)
assert(data == data2)
