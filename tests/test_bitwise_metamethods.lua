-- Test bitwise and integer division metamethods

print("=== Testing Bitwise & IDiv Metamethods ===")

local mt = {
    __band = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av & bv }, getmetatable(a) or getmetatable(b))
    end,
    __bor = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av | bv }, getmetatable(a) or getmetatable(b))
    end,
    __bxor = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av ~ bv }, getmetatable(a) or getmetatable(b))
    end,
    __bnot = function(a)
        return setmetatable({ val = ~a.val }, getmetatable(a))
    end,
    __shl = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av << bv }, getmetatable(a) or getmetatable(b))
    end,
    __shr = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av >> bv }, getmetatable(a) or getmetatable(b))
    end,
    __idiv = function(a, b)
        local av = type(a) == "table" and a.val or a
        local bv = type(b) == "table" and b.val or b
        return setmetatable({ val = av // bv }, getmetatable(a) or getmetatable(b))
    end,
    __eq = function(a, b)
        return a.val == b.val
    end,
    __tostring = function(a)
        return "Box(" .. tostring(a.val) .. ")"
    end,
}

local function box(n)
    return setmetatable({ val = n }, mt)
end

-- 1. Table-Table bitwise AND
local a = box(12) -- 1100
local b = box(10) -- 1010
local c = a & b   -- 1000 = 8
assert(c.val == 8, "__band failed")

-- 2. Table-Table bitwise OR
c = a | b         -- 1110 = 14
assert(c.val == 14, "__bor failed")

-- 3. Table-Table bitwise XOR
c = a ~ b         -- 0110 = 6
assert(c.val == 6, "__bxor failed")

-- 4. Bitwise NOT (unary)
c = ~a
assert(c.val == -13, "__bnot failed")

-- 5. Shift left
c = a << box(2)   -- 12 << 2 = 48
assert(c.val == 48, "__shl failed")

-- 6. Shift right
c = a >> box(1)   -- 12 >> 1 = 6
assert(c.val == 6, "__shr failed")

-- 7. Integer division
c = a // box(5)   -- 12 // 5 = 2
assert(c.val == 2, "__idiv failed")

-- 8. Mixed operations: Table OP Number
c = a & 10
assert(c.val == 8, "Mixed table & number failed")
c = a | 2
assert(c.val == 14, "Mixed table | number failed")
c = a ~ 10
assert(c.val == 6, "Mixed table ~ number failed")
c = a << 1
assert(c.val == 24, "Mixed table << number failed")
c = a >> 2
assert(c.val == 3, "Mixed table >> number failed")
c = a // 4
assert(c.val == 3, "Mixed table // number failed")

-- 9. Mixed operations: Number OP Table
c = 10 & a
assert(c.val == 8, "Mixed number & table failed")
c = 2 | a
assert(c.val == 14, "Mixed number | table failed")
c = 10 ~ a
assert(c.val == 6, "Mixed number ~ table failed")
c = 2 << a
assert(c.val == (2 << 12), "Mixed number << table failed")
c = 48 >> a
assert(c.val == (48 >> 12), "Mixed number >> table failed")
c = 25 // a
assert(c.val == 2, "Mixed number // table failed")

-- 10. Chained expressions
local res = (box(15) & box(7)) | (box(1) << box(3))
assert(res.val == 15, "Chained bitwise expression failed")

print("Bitwise and integer division metamethod tests passed!")
