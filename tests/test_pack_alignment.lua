-- Test string.pack alignment
print("Testing pack alignment...")

-- 1. Default alignment (1)
assert(string.packsize("bb") == 2)
assert(string.packsize("bi") == 9) -- 1 + 8

-- 2. Explicit alignment !4
assert(string.packsize("!4bb") == 4) -- 1+1 + 2(tail padding)
assert(string.packsize("!4bi") == 12) -- 1 + 3 (padding) + 8

-- 3. pack/unpack with alignment
local data = string.pack("!4bi", 0x01, 0x02)
assert(#data == 12)
local v1, v2, pos = string.unpack("!4bi", data)
assert(v1 == 0x01)
assert(v2 == 0x02)
assert(pos == 13)

print("OK: pack alignment tests passed")
