-- Test multires expressions in multiple local declarations and assignments
-- Specifically verifies VarargExprNode (...) in last position

print("=== Testing multires and vararg assignments ===")

-- 1. Varargs in multiple local declaration: local a, b = ...
local function test_local_decl(...)
    local a, b = ...
    local pos = 100 -- Ensure subsequent locals are NOT corrupted!
    return a, b, pos
end

local v1, v2, p = test_local_decl("first", "second")
assert(v1 == "first", "v1 should be 'first'")
assert(v2 == "second", "v2 should be 'second'")
assert(p == 100, "pos should be 100")

-- 2. Varargs with more vars than args (should pad with nil)
local v1, v2, v3, p2 = (function(...)
    local a, b, c = ...
    local tag = "safe"
    return a, b, c, tag
end)("only_one")

assert(v1 == "only_one")
assert(v2 == nil)
assert(v3 == nil)
assert(p2 == "safe")

-- 3. Varargs with fewer vars than args (should discard excess)
local x, y, z = (function(...)
    local a, b = ...
    local final = 99
    return a, b, final
end)(1, 2, 3, 4, 5)

assert(x == 1)
assert(y == 2)
assert(z == 99)

-- 4. Multiple assignment: a, b = ...
local function test_assign(...)
    local a, b, c = 0, 0, 0
    a, b = ...
    c = 77
    return a, b, c
end
local a1, a2, a3 = test_assign("apple", "banana")
assert(a1 == "apple")
assert(a2 == "banana")
assert(a3 == 77)

-- 5. Multiple initializers with vararg as last: local a, b, c = 1, ...
local function test_mixed_decl(...)
    local a, b, c = 1, ...
    local end_var = "ok"
    return a, b, c, end_var
end
local m1, m2, m3, m4 = test_mixed_decl(2, 3)
assert(m1 == 1)
assert(m2 == 2)
assert(m3 == 3)
assert(m4 == "ok")

-- 6. Closures capturing vararg-initialized locals (upvalues)
local function make_getter(...)
    local s, p = ...
    local pos = 1
    return function()
        return s, p, pos
    end
end
local getter = make_getter("hello", "pattern")
local g_s, g_p, g_pos = getter()
assert(g_s == "hello")
assert(g_p == "pattern")
assert(g_pos == 1)

print("Multires and vararg assignment tests passed!")
