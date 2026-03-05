-- Simple test for os library
print("Testing os library...")

local test_file1 = "test_file1.txt"
local test_file2 = "test_file2.txt"

local function create_file(path)
    local f = io.open(path, "w")
    if f then
        f:write("test data")
        f:close()
    end
end

-- Clean up any left-overs from previous runs
os.remove(test_file1)
os.remove(test_file2)

-- test os.rename
create_file(test_file1)
local ok, err = os.rename(test_file1, test_file2)
assert(ok == true, "os.rename should succeed")

-- In some environments rename might be delayed, but standard Lua behavior is synchronous.
-- We'll just check if test_file2 exists.
local f2 = io.open(test_file2, "r")
assert(f2 ~= nil, "new file should exist")
if f2 then f2:close() end

-- test os.remove
ok, err = os.remove(test_file2)
assert(ok == true, "os.remove should succeed")

-- test os.time
local t = os.time()
assert(type(t) == "number")

-- test os.date
local d = os.date("*t", t)
assert(type(d) == "table")
assert(d.year >= 2024)

-- test os.clock
local c = os.clock()
assert(type(c) == "number")

-- test os.getenv
local path = os.getenv("PATH")
if path then
    assert(type(path) == "string")
end

-- test os.execute
local res = os.execute("ls > /dev/null")
assert(res == true or res == 0)

print("os tests passed!")
