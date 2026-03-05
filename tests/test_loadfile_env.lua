-- Test loadfile with explicit environment
print("Testing loadfile(path, mode, env)...")

local test_file = "temp_loadfile_env.lua"
local fh = io.open(test_file, "w")
fh:write("return x")
fh:close()

-- 1. Test environment isolation
local env = { x = "hello" }
local f, err = loadfile(test_file, "t", env)
assert(f ~= nil, err)
assert(f() == "hello")
print("OK: Environment isolation")

-- 2. cleanup
os.remove(test_file)

print("loadfile with env tests passed!")
