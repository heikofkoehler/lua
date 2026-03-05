-- Complete OS Library Tests
print("Testing OS Library...")

-- time/date
local t = os.time()
assert(type(t) == "number")
local d = os.date("*t", t)
assert(type(d) == "table")
assert(d.year >= 2024)

-- clock
local c = os.clock()
assert(type(c) == "number")

-- difftime
assert(os.difftime(100, 50) == 50)

-- getenv
local path = os.getenv("PATH")
if path then assert(type(path) == "string") end

-- remove/rename
local test_file = "tests/temp_os_test.txt"
local f = io.open(test_file, "w")
f:write("hello")
f:close()
os.rename(test_file, test_file .. ".new")
local f2 = io.open(test_file .. ".new", "r")
assert(f2 ~= nil)
f2:close()
os.remove(test_file .. ".new")

-- execute
local res = os.execute("ls > /dev/null")
assert(res == true or res == 0)

-- tmpname
local tmp = os.tmpname()
assert(type(tmp) == "string")

print("OS Library Tests Passed!")
