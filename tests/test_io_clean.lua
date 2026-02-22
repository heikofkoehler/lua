-- Clean IO test

-- Write to file
print("Writing to output.txt...")
local outfile = io_open("output.txt", "w")
io_write(outfile, "Hello from Lua!")
io_write(outfile, " ")
io_write(outfile, "This is line 2")
io_close(outfile)
print("Done writing")

-- Read from file
print("Reading from output.txt...")
local infile = io_open("output.txt", "r")
local data = io_read(infile)
io_close(infile)

-- Print what we read
print("File contents:")
print(data)
