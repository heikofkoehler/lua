-- Complete Table Library Tests
print("Testing Table Library...")

-- Basic operations
local t = {1, 2, 3}
table.insert(t, 4)
assert(#t == 4)
assert(t[4] == 4)
table.insert(t, 1, 0)
assert(t[1] == 0)
assert(t[2] == 1)
assert(#t == 5)

local r = table.remove(t)
assert(r == 4)
assert(#t == 4)
local r2 = table.remove(t, 1)
assert(r2 == 0)
assert(t[1] == 1)

-- Concat
local t2 = {"a", "b", "c"}
assert(table.concat(t2, ",") == "a,b,c")
assert(table.concat(t2) == "abc")

-- Pack/Unpack
local t3 = table.pack(1, 2, 3)
assert(t3.n == 3)
assert(t3[1] == 1)
local a, b, c = table.unpack(t3)
assert(a == 1 and b == 2 and c == 3)

-- Sort
local t4 = {3, 1, 4, 1, 5, 9}
table.sort(t4)
assert(t4[1] == 1 and t4[2] == 1 and t4[3] == 3 and t4[4] == 4 and t4[5] == 5 and t4[6] == 9)

-- Move
local t5 = {10, 20, 30}
local t6 = {}
table.move(t5, 1, 3, 1, t6)
assert(t6[1] == 10 and t6[2] == 20 and t6[3] == 30)

print("Table Library Tests Passed!")
