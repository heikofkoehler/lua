-- Test io.popen

print("Testing io.popen read...")
local f = io.popen("echo hello", "r")
assert(f)
local res = f:read("a")
print("Got:", string.format("%q", res))
assert(string.find(res, "hello"))
f:close()
print("OK")

print("All io.popen tests passed!")
