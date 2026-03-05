-- Test load with explicit environment
print("Testing load(chunk, name, mode, env)...")

-- 1. Test basic environment isolation
local env = { x = 100, print = print }
local f = load("return x", "test", "t", env)
assert(f() == 100)
print("OK: Environment isolation")

-- 2. Test chunkname
local f2, err = load("error('msg')", "my_chunk_name")
local ok, err = pcall(f2)
assert(ok == false)
assert(string.find(err, "my_chunk_name"))
print("OK: Chunk name")

-- 3. Test nil env (defaults to _G)
_G.global_x = 42
local f3 = load("return global_x", "test", "t", nil)
assert(f3() == 42)
print("OK: Default environment")

print("load with env tests passed!")
