print("Testing os library...")

-- os.time and os.difftime
local t1 = os.time()
assert(type(t1) == "number", "os.time() should return a number")
local t2 = t1 + 5
local diff = os.difftime(t2, t1)
assert(diff == 5, "os.difftime should return the difference in seconds")

-- os.clock
local c = os.clock()
assert(type(c) == "number", "os.clock() should return a number")
assert(c >= 0, "os.clock() should be non-negative")

-- os.getenv
local path = os.getenv("PATH")
assert(type(path) == "string" or path == nil, "os.getenv should return string or nil")
local nonexistent = os.getenv("SOME_NONEXISTENT_VAR_12345")
assert(nonexistent == nil, "os.getenv should return nil for nonexistent vars")

-- File operations setup
local test_file1 = "test_os_file1.tmp"
local test_file2 = "test_os_file2.tmp"

-- Helper to create a file
local function create_file(name)
    local f = io.open(name, "w")
    if f then
        io.write(f, "test data")
        io.close(f)
    end
end

-- Clean up any left-overs from previous runs
os.remove(test_file1)
os.remove(test_file2)

-- test os.rename
create_file(test_file1)
local ok, err = os.rename(test_file1, test_file2)
assert(ok == true, "os.rename should succeed")

local f1 = io.open(test_file1, "r")
assert(f1 == nil, "old file should not exist")

local f2 = io.open(test_file2, "r")
assert(f2 ~= nil, "new file should exist")
if f2 then io.close(f2) end

-- test os.remove
ok, err = os.remove(test_file2)
assert(ok == true, "os.remove should succeed")

local f3 = io.open(test_file2, "r")
assert(f3 == nil, "removed file should not exist")

ok, err = os.remove("some_nonexistent_file_xyz.tmp")
assert(ok == nil, "os.remove on nonexistent file should return nil")
assert(type(err) == "string", "os.remove should return error message")

print("os library tests passed!")
