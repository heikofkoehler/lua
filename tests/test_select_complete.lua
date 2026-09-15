-- Test select function thoroughly (positive, negative, multires, count)

print("=== Testing select complete ===")

-- 1. select("#", ...)
assert(select("#") == 0)
assert(select("#", "a") == 1)
assert(select("#", 1, 2, 3, 4, 5) == 5)
assert(select("#", nil, nil) == 2)
assert(select("#", nil, "x", nil) == 3)

-- 2. select(n, ...) with positive indices
local function pack(...)
    return { ... }
end

local t1 = pack(select(1, "a", "b", "c"))
assert(#t1 == 3)
assert(t1[1] == "a" and t1[2] == "b" and t1[3] == "c")

local t2 = pack(select(2, "a", "b", "c"))
assert(#t2 == 2)
assert(t2[1] == "b" and t2[2] == "c")

local t3 = pack(select(3, "a", "b", "c"))
assert(#t3 == 1)
assert(t3[1] == "c")

local t4 = pack(select(4, "a", "b", "c"))
assert(#t4 == 0)

-- 3. select(n, ...) with negative indices
local tn1 = pack(select(-1, "a", "b", "c"))
assert(#tn1 == 1)
assert(tn1[1] == "c")

local tn2 = pack(select(-2, "a", "b", "c"))
assert(#tn2 == 2)
assert(tn2[1] == "b" and tn2[2] == "c")

local tn3 = pack(select(-3, "a", "b", "c"))
assert(#tn3 == 3)
assert(tn3[1] == "a" and tn3[2] == "b" and tn3[3] == "c")

-- 4. Out of bounds index error
local ok, err = pcall(select, 0, "a", "b")
assert(ok == false, "select(0) should error")

ok, err = pcall(select, -4, "a", "b", "c")
assert(ok == false, "select(-4) with 3 args should error")

-- 5. select inside functions and with varargs
local function last(...)
    return select(-1, ...)
end
assert(last(10, 20, 30, 40) == 40)
assert(last("single") == "single")

local function tail(...)
    return select(2, ...)
end
local r1, r2, r3 = tail(1, 2, 3, 4)
assert(r1 == 2 and r2 == 3 and r3 == 4)

print("All select tests passed!")
