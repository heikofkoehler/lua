-- Deep testing of standard library functionalities

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %s, got %s. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing String Pack/Unpack ===")
local packed = string.pack(">i4", 0x12345678)
assert_eq(#packed, 4)
local unpacked = string.unpack(">i4", packed)
assert_eq(unpacked, 0x12345678)

print("=== Testing Table Move ===")
local t1 = {1, 2, 3}
local t2 = {10, 20, 30}
table.move(t1, 1, 2, 2, t2)
assert_eq(t2[1], 10)
assert_eq(t2[2], 1)
assert_eq(t2[3], 2)

print("=== Testing Table Sort with Comparator ===")
local t3 = {3, 1, 4, 1, 5, 9}
table.sort(t3, function(a, b) return a > b end)
assert_eq(t3[1], 9)
assert_eq(t3[2], 5)
assert_eq(t3[6], 1)

print("=== Testing Coroutine Close ===")
local co = coroutine.create(function()
    coroutine.yield()
end)
coroutine.resume(co)
assert_eq(coroutine.status(co), "suspended")
local ok, err = coroutine.close(co)
assert_eq(ok, true)
assert_eq(coroutine.status(co), "dead")

print("=== Testing UTF8 Library ===")
local s = "hello"
local count = 0
for p, c in utf8.codes(s) do
    count = count + 1
end
assert_eq(count, 5)
assert_eq(utf8.len("abc"), 3)

print("=== Testing Math Random Range ===")
math.randomseed(1234)
for i=1, 100 do
    local r = math.random(10, 20)
    assert(r >= 10 and r <= 20)
end

print("=== Testing IO Popen ===")
local f = io.popen("echo hello_popen", "r")
if f then
    local res = f:read("l")
    assert_eq(res, "hello_popen")
    f:close()
end

print("=== Testing Debug GetInfo ===")
local function test_func(a, b) return a + b end
local info = debug.getinfo(test_func)
assert_eq(info.name, "test_func")
assert_eq(info.nparams, 2)

print("=== Testing File:lines ===")
local fname = "temp_lines.txt"
local fw = io.open(fname, "w")
fw:write("line1\nline2\nline3\n")
fw:close()

local count_test = 0
local fr = io.open(fname, "r")
for line in fr:lines() do
    count_test = count_test + 1
    assert_eq(line, "line" .. count_test)
end
fr:close()
os.remove(fname)
assert_eq(count_test, 3)

print("=== Testing Package SearchPath ===")
local path = "?.lua;tests/?.lua"
local res, err = package.searchpath("test_base_complete", path)
assert(res ~= nil)
assert(string.find(res, "test_base_complete.lua"))

print("=== Testing OS Clock/Getenv ===")
assert(type(os.clock()) == "number")
assert(os.getenv("PATH") ~= nil)

print("\nDEEP STDLIB TESTS PASSED!")
