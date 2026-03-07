-- Test IO streams (io.input, io.output)

print("Testing IO input/output streams...")

local fname = "test_streams.txt"
local f = io.open(fname, "w")
f:write("stream data\n")
f:close()

-- Test io.input(filename)
print("Testing io.input(filename)...")
local old_in = io.input()
local res_in = io.input(fname)
assert(io.type(res_in) == "file")
assert(io.input() == res_in)

local line = io.read()
assert(line == "stream data")

-- Restore old input
io.input(old_in)

-- Test io.output(filename)
print("Testing io.output(filename)...")
local out_name = "test_out.txt"
local old_out = io.output()
local res_out = io.output(out_name)
assert(io.type(res_out) == "file")
assert(io.output() == res_out)

io.write("output data")
io.flush()
io.output():close() -- standard behavior might vary, but we'll close it manually

-- Verify output
local f_check = io.open(out_name, "r")
assert(f_check:read("*a") == "output data")
f_check:close()

-- Restore old output
io.output(old_out)

os.remove(fname)
os.remove(out_name)

print("IO streams tests passed!")
