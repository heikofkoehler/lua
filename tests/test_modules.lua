-- Test script for external C modules: luafilesystem and lua-cjson
package.cpath = "./tests/fixtures/lfs/?.so;./tests/fixtures/cjson/?.so;./fixtures/lfs/?.so;./fixtures/cjson/?.so;" .. package.cpath

-- 1. Test LuaFileSystem (LFS)
print("=== Testing LuaFileSystem (LFS) ===")
local lfs = require("lfs")
assert(type(lfs) == "table", "lfs must be a table")
assert(type(lfs._VERSION) == "string", "lfs._VERSION must be string")
print("LFS version:", lfs._VERSION)

local current = lfs.currentdir()
assert(type(current) == "string" and #current > 0, "currentdir must return non-empty string")
print("Current dir:", current)

-- Find a known target file and directory
local cmake_file = "CMakeLists.txt"
local target_dir = "src"
if not io.open(cmake_file, "r") then
    cmake_file = "../CMakeLists.txt"
    target_dir = "../src"
end

-- Test directory iteration
local dir_count = 0
for entry in lfs.dir(".") do
    dir_count = dir_count + 1
end
assert(dir_count > 0, "lfs.dir must find entries")
print("Directory entries found:", dir_count)

-- Test file attributes
local attr = lfs.attributes(cmake_file)
assert(type(attr) == "table", "attributes must return a table")
assert(attr.mode == "file", "CMakeLists.txt mode must be 'file'")
assert(type(attr.size) == "number" and attr.size > 0, "size must be positive number")
assert(type(attr.modification) == "number", "modification must be timestamp")
print("File attributes: size =", attr.size, "mode =", attr.mode)

-- Test directory attributes
local dir_attr = lfs.attributes(target_dir)
assert(dir_attr.mode == "directory", "target_dir mode must be 'directory'")

-- Test setmode with a Lua FILE*
local f = io.open(cmake_file, "r")
assert(f ~= nil, "failed to open cmake_file")
local sm_res = lfs.setmode(f, "binary")
assert(sm_res == true, "setmode must succeed on valid FILE*")
f:close()
print("LFS check_file and luaL_Stream setmode: OK")

-- 2. Test lua-cjson
print("\n=== Testing lua-cjson ===")
local cjson = require("cjson")
assert(type(cjson) == "table", "cjson must be a table")
assert(type(cjson._VERSION) == "string", "cjson._VERSION must be string")
print("cjson version:", cjson._VERSION)

-- Test basic encode / decode
local test_data = {
    str = "hello world",
    int = 42,
    num = 3.14159,
    bool_true = true,
    bool_false = false,
    list = { 10, 20, 30 }
}
local json_str = cjson.encode(test_data)
assert(type(json_str) == "string", "encode must return string")
print("Encoded JSON:", json_str)

local decoded = cjson.decode(json_str)
assert(type(decoded) == "table", "decode must return table")
assert(decoded.str == "hello world")
assert(decoded.int == 42)
assert(math.abs(decoded.num - 3.14159) < 1e-5)
assert(decoded.bool_true == true)
assert(decoded.bool_false == false)
assert(#decoded.list == 3)
assert(decoded.list[1] == 10 and decoded.list[2] == 20 and decoded.list[3] == 30)
print("Decoded data matches original: OK")

-- Test null handling
assert(cjson.null ~= nil, "cjson.null must be non-nil lightuserdata")
local with_null = cjson.decode('{"val": null}')
assert(with_null.val == cjson.null, "null must decode to cjson.null")
print("Null handling: OK")

-- Test pcall error handling with cjson
local ok, err = pcall(cjson.decode, "{invalid json")
assert(not ok, "pcall on invalid JSON must fail")
assert(type(err) == "string", "error must be string")
print("cjson error handling via pcall: OK (error:", err:sub(1, 35) .. "...)")

-- Test cjson.safe
print("STEP A: require cjson.safe")
local cjson_safe = require("cjson.safe")
print("STEP B: cjson.safe.decode valid")
local s_res, s_err = cjson_safe.decode('{"valid": 99}')
print("STEP C: check s_res")
assert(type(s_res) == "table" and s_res.valid == 99, "cjson.safe must decode valid json")
print("STEP D: cjson.safe.decode bad")
local bad_res, bad_err = cjson_safe.decode("{invalid json")
print("STEP E: check bad_res")
assert(bad_res == nil, "cjson.safe must return nil on error")
assert(type(bad_err) == "string", "cjson.safe must return error string")
print("cjson.safe return on error: OK (err:", bad_err:sub(1, 35) .. "...)")

print("\n=== ALL C MODULE TESTS PASSED SUCCESSFULLY! ===")
