-- Test load mode enforcement
print("Testing load mode enforcement...")

-- 1. Mode 't' (text only)
local f_text = load("return 42", "text_chunk", "t")
assert(type(f_text) == "function")
assert(f_text() == 42)
print("OK: load text with mode 't'")

-- 2. Mode 'b' (binary only) - should fail for text source
local f_bin, err = load("return 42", "text_chunk", "b")
if f_bin == nil then
    print("OK: load text with mode 'b' failed as expected: " .. tostring(err))
else
    print("FAIL: load text with mode 'b' should have failed!")
end

-- 3. Binary bytecode test
local bytecode = string.dump(function() return 123 end)
local f_b, err_b = load(bytecode, "bin_chunk", "b")
assert(type(f_b) == "function")
assert(f_b() == 123)
print("OK: load binary with mode 'b'")

-- 4. Binary with mode 't' should fail
local f_t, err2 = load(bytecode, "bin_chunk", "t")
if f_t == nil then
    print("OK: load binary with mode 't' failed as expected: " .. tostring(err2))
else
    print("FAIL: load binary with mode 't' should have failed!")
end

print("Load mode tests completed.")
