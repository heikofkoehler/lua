-- Test io.read multi-value and EOF behavior

print("Testing io.read multi-value and EOF...")

local fname = "test_io_multi.txt"
local f = io.open(fname, "w")
f:write("123 line1\n456 line2")
f:close()

local f2 = io.open(fname, "r")
-- Test reading number and line together
local n, l = f2:read("*n", "*l")
assert(n == 123)
assert(l == " line1")

-- Test reading number and EOF
local n2, l2 = f2:read("*n", "*l")
assert(n2 == 456)
assert(l2 == " line2")

-- Test reading at EOF
local n3, l3 = f2:read("*n", "*l")
assert(n3 == nil)
assert(l3 == nil)

f2:close()
os.remove(fname)

-- Test reading with count
local f3 = io.open("test_count.txt", "w")
f3:write("abcdef")
f3:close()

local f4 = io.open("test_count.txt", "r")
local s1, s2 = f4:read(2, 3)
assert(s1 == "ab")
assert(s2 == "cde")
local s3 = f4:read(10) -- more than remaining
assert(s3 == "f")
local s4 = f4:read(1) -- already at EOF
assert(s4 == nil)
f4:close()
os.remove("test_count.txt")

print("io.read tests passed!")
