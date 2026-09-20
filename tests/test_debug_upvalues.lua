-- Test debug upvalue manipulation, registry, and userdata values

print("=== Testing debug upvalues & userdata ===")

-- 1. Upvalue identification and upvaluejoin
local a = 10
local b = 20

local function f1()
    return a
end

local function f2()
    return b
end

-- f1 upvalues: 1 is a
-- f2 upvalues: 1 is b
local id1 = debug.upvalueid(f1, 1)
local id2 = debug.upvalueid(f2, 1)
assert(id1 ~= nil, "upvalueid should return non-nil")
assert(id2 ~= nil, "upvalueid should return non-nil")
assert(id1 ~= id2, "distinct upvalues should have distinct IDs")

-- Join f1's upvalue 1 to f2's upvalue 1
debug.upvaluejoin(f1, 1, f2, 1)
local id1_after = debug.upvalueid(f1, 1)
assert(id1_after == id2, "joined upvalues should share the same ID")

-- Verifying shared mutation
debug.setupvalue(f2, 1, 999)
assert(f1() == 999, "f1 should see mutation made via f2")
assert(f2() == 999, "f2 should see mutation")

debug.setupvalue(f1, 1, 42)
assert(f1() == 42)
assert(f2() == 42)

-- 2. Error cases for upvalueid and upvaluejoin
assert(debug.upvalueid(f1, 99) == nil, "out-of-bounds upvalueid should return nil")

ok, err = pcall(debug.upvaluejoin, f1, 10, f2, 2)
assert(ok == false, "out-of-bounds f1 upvaluejoin should error")

ok, err = pcall(debug.upvaluejoin, f1, 2, f2, 10)
assert(ok == false, "out-of-bounds f2 upvaluejoin should error")

-- 3. debug.getuservalue and setuservalue
local ud = __test_userdata(50)
assert(type(ud) == "userdata")
assert(debug.getuservalue(ud) == nil)

local data = { tag = "test_payload", score = 100 }
debug.setuservalue(ud, data)
local retrieved = debug.getuservalue(ud)
assert(type(retrieved) == "table")
assert(retrieved.tag == "test_payload")
assert(retrieved.score == 100)

-- 4. debug.getregistry
local reg = debug.getregistry()
assert(type(reg) == "table")

print("Debug upvalues and userdata tests passed!")
