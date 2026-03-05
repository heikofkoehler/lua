-- Test <const> attribute
print("Testing <const> attribute...")

-- 1. Simple local const
local x <const> = 10
assert(x == 10)

-- 2. Multiple local const
local a <const>, b, c <const> = 1, 2, 3
assert(a == 1)
assert(b == 2)
assert(c == 3)

-- 3. Const in a closure (upvalue)
local y <const> = 20
local function f()
    return y
end
assert(f() == 20)

-- 4. Reassignment should fail (compile-time error)
local function test_reassign()
    local source = [[
        local x <const> = 10
        x = 20
    ]]
    local f, err = load(source)
    assert(f == nil)
    assert(string.find(err, "attempt to assign to const variable 'x'"))
end
test_reassign()

-- 5. Multiple assignment reassignment should fail
local function test_multi_reassign()
    local source = [[
        local a <const>, b = 1, 2
        a, b = 3, 4
    ]]
    local f, err = load(source)
    assert(f == nil)
    assert(string.find(err, "attempt to assign to const variable 'a'"))
end
test_multi_reassign()

-- 6. Upvalue reassignment should fail
local function test_upvalue_reassign()
    local source = [[
        local x <const> = 10
        local function f()
            x = 20
        end
    ]]
    local f, err = load(source)
    assert(f == nil)
    assert(string.find(err, "attempt to assign to const variable 'x'"))
end
test_upvalue_reassign()

print("<const> attribute tests passed!")
