-- Test string.pack depth (tail padding and alignment)
print("Testing string.pack alignment depth...")

-- 1. Without Xi4: no tail padding
local p1 = string.pack("!4b", 1)
assert(#p1 == 1)
local p1_tail = string.pack("!4bXi4", 1)
assert(#p1_tail == 4)

-- 2. Complex alignment
-- "!4bjb" -> 1 (b) + 3 (pad) + 8 (j) + 1 (b) = 13 (no tail pad)
local p2 = string.pack("!4bjb", 1, 2, 3)
assert(#p2 == 13)
-- "!4bjbXi4" -> 13 + 3 (tail pad) = 16
local p2_tail = string.pack("!4bjbXi4", 1, 2, 3)
assert(#p2_tail == 16)

-- 3. string.packsize should match
assert(string.packsize("!4b") == 1)
assert(string.packsize("!4bXi4") == 4)
assert(string.packsize("!4bjb") == 13)
assert(string.packsize("!4bjbXi4") == 16)
assert(string.packsize("!4bib") == 9)
assert(string.packsize("!4bibXi4") == 12)

print("OK: string.pack depth tests passed")
