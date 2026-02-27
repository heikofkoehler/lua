-- Test debug and string stubs

print("Testing debug.sethook...")
debug.sethook(function() end, "cr")
print("OK")

print("Testing string.packsize...")
assert(string.packsize("j") == 8)
assert(string.packsize("n") == 8)
print("OK")

print("Testing string.dump...")
local d = string.dump(function() end)
assert(type(d) == "string")
assert(d == "function_bytecode_stub")
print("OK")

print("Testing string.pack...")
local p = string.pack("i4", 42)
assert(type(p) == "string")
assert(p == "packed_data_stub")
print("OK")

print("All stub tests passed!")
