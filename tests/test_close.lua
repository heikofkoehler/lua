-- Test <close> attribute
print("Testing <close> attribute...")

local closed_count = 0
local function create_closable(name)
    return setmetatable({name = name}, {
        __close = function(self, err)
            print("  - Closing " .. self.name .. (err and (" with error: " .. tostring(err)) or ""))
            closed_count = closed_count + 1
        end
    })
end

-- 1. Simple block scope
do
    local x <close> = create_closable("x")
    assert(closed_count == 0)
end
assert(closed_count == 1)
print("OK: Simple block scope")

-- 2. Multiple <close> variables (should close in reverse order)
closed_count = 0
do
    local a <close> = create_closable("a")
    local b <close> = create_closable("b")
end
assert(closed_count == 2)
print("OK: Multiple variables")

-- 3. Closing on function return
closed_count = 0
local function test_ret()
    local y <close> = create_closable("y")
    return 42
end
local res = test_ret()
assert(res == 42)
assert(closed_count == 1)
print("OK: Function return")

-- 4. Nested scopes
closed_count = 0
do
    local outer <close> = create_closable("outer")
    do
        local inner <close> = create_closable("inner")
        assert(closed_count == 0)
    end
    assert(closed_count == 1)
end
assert(closed_count == 2)
print("OK: Nested scopes")

-- 5. Integration with <const> (variables marked <close> are also <const>)
local function test_close_is_const()
    local source = [[
        local x <close> = {}
        x = 20
    ]]
    local f, err = load(source)
    assert(f == nil)
    assert(string.find(err, "attempt to assign to const variable 'x'"))
end
test_close_is_const()
print("OK: <close> is implicitly <const>")

print("<close> attribute tests passed!")
