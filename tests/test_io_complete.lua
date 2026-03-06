-- Test io.read and io.lines
print("Testing io.read and io.lines...")

local f = io.open("test_io_temp.txt", "w")
f:write("line 1\nline 2\nline 3\n")
f:close()

-- Test io.lines with format
print("Testing io.lines...")
local count = 0
for line in io.lines("test_io_temp.txt") do
    count = count + 1
    assert(line == "line " .. count)
end
assert(count == 3)

-- Test io.read with multiple formats
print("Testing io.read with multiple formats...")
local f2 = io.open("test_io_temp.txt", "r")
local l1, l2 = f2:read("l", "l")
assert(l1 == "line 1")
assert(l2 == "line 2")

local l3 = f2:read(4)
assert(l3 == "line")
f2:close()

-- Test reading all
local f3 = io.open("test_io_temp.txt", "r")
local all = f3:read("*a")
assert(all == "line 1\nline 2\nline 3\n")
f3:close()

os.remove("test_io_temp.txt")
print("OK: io completeness passed")
