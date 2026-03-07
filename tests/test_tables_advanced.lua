-- Advanced Table Library Tests: Corner Cases and Lua 5.4 Features

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %s, got %s. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing Table Move Corner Cases ===")
-- 1. Overlapping move (forward)
local t1 = {1, 2, 3, 4, 5}
table.move(t1, 1, 3, 2)
assert_eq(t1[1], 1)
assert_eq(t1[2], 1)
assert_eq(t1[3], 2)
assert_eq(t1[4], 3)
assert_eq(t1[5], 5)

-- 2. Overlapping move (backward)
local t2 = {1, 2, 3, 4, 5}
table.move(t2, 2, 4, 1)
assert_eq(t2[1], 2)
assert_eq(t2[2], 3)
assert_eq(t2[3], 4)
assert_eq(t2[4], 4)
assert_eq(t2[5], 5)

-- 3. Move from one table to another
local src = {10, 20}
local dest = {1, 2, 3}
table.move(src, 1, 2, 2, dest)
assert_eq(dest[1], 1)
assert_eq(dest[2], 10)
assert_eq(dest[3], 20)

print("=== Testing Table Pack/Unpack with Nils ===")
-- Pack handles nils via the 'n' field
local tp = table.pack(1, nil, 3)
assert_eq(tp.n, 3)
assert_eq(tp[1], 1)
assert_eq(tp[2], nil)
assert_eq(tp[3], 3)

-- Unpack with explicit range
local a, b, c, d = table.unpack(tp, 1, 4)
assert_eq(a, 1)
assert_eq(b, nil)
assert_eq(c, 3)
assert_eq(d, nil)

print("=== Testing Table Sort stability and custom comparators ===")
local t_sort = {
    {name="apple", price=10},
    {name="banana", price=5},
    {name="cherry", price=5},
    {name="date", price=20}
}
table.sort(t_sort, function(a, b)
    if a.price ~= b.price then
        return a.price < b.price
    end
    return a.name < b.name
end)
assert_eq(t_sort[1].name, "banana")
assert_eq(t_sort[2].name, "cherry")
assert_eq(t_sort[3].name, "apple")
assert_eq(t_sort[4].name, "date")

print("=== Testing Table Concat with non-string keys ===")
local tc = {1, 2, 3}
assert_eq(table.concat(tc, "-"), "1-2-3")
local ok, err = pcall(table.concat, {1, {}, 3})
assert_eq(ok, false) -- should fail because table cannot be converted to string automatically in concat

print("=== Testing Table Insert/Remove Out of Bounds ===")
local t_bounds = {1, 2, 3}
local ok_i, err_i = pcall(table.insert, t_bounds, 5, 4)
assert_eq(ok_i, false)
local ok_r, err_r = pcall(table.remove, t_bounds, 5)
assert_eq(ok_r, false)

print("\nAdvanced table tests passed!")
