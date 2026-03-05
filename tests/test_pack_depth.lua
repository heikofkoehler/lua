-- Test string.pack depth (tail padding and alignment)
print("Testing string.pack alignment depth...")

-- 1. Tail padding for !n
-- In Lua 5.4, string.pack("!4 b", 1) should be 4 bytes (aligned to 4)
local p1 = string.pack("!4b", 1)
print("pack('!4b', 1) size:", #p1)
assert(#p1 == 4) 

-- 2. Complex alignment
-- "!4 b i b" -> 1 (b) + 3 (pad) + 8 (i) + 1 (b) + 3 (tail pad) = 16
local p2 = string.pack("!4bib", 1, 2, 3)
print("pack('!4bib', ...) size:", #p2)
assert(#p2 == 16)

-- 3. string.packsize should match
assert(string.packsize("!4b") == 4)
assert(string.packsize("!4bib") == 16)

print("OK: string.pack depth tests passed")
