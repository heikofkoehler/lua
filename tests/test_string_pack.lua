-- Test string.pack and string.unpack

print("Testing string.packsize...")
assert(string.packsize("b") == 1)
assert(string.packsize("i") == 8)
assert(string.packsize("n") == 8)
assert(string.packsize("bb") == 2)
print("OK")

print("Testing string.pack/unpack basic...")
local data = string.pack("bi", 0x41, 12345678)
assert(#data == 9)
local b, i, next_pos = string.unpack("bi", data)
assert(b == 0x41)
assert(i == 12345678)
assert(next_pos == 10)
print("OK")

print("Testing string.pack/unpack float...")
local data2 = string.pack("dn", 1.5, -2.25)
assert(#data2 == 16)
local d, n, next_pos2 = string.unpack("dn", data2)
assert(d == 1.5)
assert(n == -2.25)
assert(next_pos2 == 17)
print("OK")

print("Testing string.pack/unpack string...")
local data3 = string.pack("sz", "hello", "world")
-- 8 bytes len + 5 bytes "hello" + 5 bytes "world" + 1 byte null = 19
assert(#data3 == 19)
local s, z, next_pos3 = string.unpack("sz", data3)
assert(s == "hello")
assert(z == "world")
assert(next_pos3 == 20)
print("OK")

print("All string.pack/unpack tests passed!")
