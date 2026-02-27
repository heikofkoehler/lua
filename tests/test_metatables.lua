-- Test metatables and metamethods

print("Testing setmetatable and getmetatable...")
local t = {}
local mt = { name = "mymeta" }
assert(setmetatable(t, mt) == t)
assert(getmetatable(t) == mt)
assert(getmetatable(t).name == "mymeta")
assert(setmetatable(t, nil) == t)
assert(getmetatable(t) == nil)

print("Testing __index metamethod (table)...")
local base = { x = 10, y = 20 }
local proxy = setmetatable({}, { __index = base })
assert(proxy.x == 10)
assert(proxy.y == 20)
assert(proxy.z == nil)

print("Testing __index metamethod (function)...")
local func_index = function(table, key)
    if key == "answer" then return 42 end
    return "unknown"
end
local t2 = setmetatable({}, { __index = func_index })
assert(t2.answer == 42)
assert(t2.question == "unknown")

print("Testing __newindex metamethod (table)...")
local storage = {}
local t3 = setmetatable({}, { __newindex = storage })
t3.a = 100
assert(t3.a == nil)
assert(storage.a == 100)

print("Testing __newindex metamethod (function)...")
local log = {}
local func_newindex = function(table, key, value)
    log[key] = value
end
local t4 = setmetatable({}, { __newindex = func_newindex })
t4.score = 50
assert(t4.score == nil)
assert(log.score == 50)

print("Testing arithmetic metamethods...")
local vec_mt = {
    __add = function(a, b)
        return { x = a.x + b.x, y = a.y + b.y }
    end,
    __sub = function(a, b)
        return { x = a.x - b.x, y = a.y - b.y }
    end,
    __mul = function(a, b)
        if type(a) == "number" then
            return { x = a * b.x, y = a * b.y }
        else
            return { x = a.x * b, y = a.y * b }
        end
    end,
    __unm = function(a)
        return { x = -a.x, y = -a.y }
    end
}

local vec = function(x, y)
    return setmetatable({ x = x, y = y }, vec_mt)
end

local v1 = vec(1, 2)
local v2 = vec(3, 4)
local v3 = v1 + v2
assert(v3.x == 4)
assert(v3.y == 6)

local v4 = v2 - v1
assert(v4.x == 2)
assert(v4.y == 2)

-- Test unary minus
local v5 = -v1
assert(v5.x == -1)
assert(v5.y == -2)

print("Testing comparison metamethods...")
local comp_mt = {
    __eq = function(a, b) return a.val == b.val end,
    __lt = function(a, b) return a.val < b.val end,
    __le = function(a, b) return a.val <= b.val end
}

local box = function(val)
    return setmetatable({ val = val }, comp_mt)
end

local b1 = box(10)
local b2 = box(20)
local b3 = box(10)

assert(b1 == b3)
assert(b1 ~= b2)
assert(b1 < b2)
assert(b2 > b1)
assert(b1 <= b2)
assert(b1 <= b3)
assert(b2 >= b1)

print("Testing __call metamethod...")
local callable = setmetatable({}, {
    __call = function(table, a, b)
        return a + b
    end
})
assert(callable(10, 20) == 30)

print("Testing __tostring metamethod...")
local t6 = setmetatable({ val = 123 }, {
    __tostring = function(table)
        return "Object(" .. table.val .. ")"
    end
})
assert(tostring(t6) == "Object(123)")

print("Testing __concat metamethod...")
local t7 = setmetatable({ str = "hello" }, {
    __concat = function(a, b)
        local s1 = type(a) == "table" and a.str or tostring(a)
        local s2 = type(b) == "table" and b.str or tostring(b)
        return s1 .. s2
    end
})
assert(t7 .. " world" == "hello world")
assert("hi " .. t7 == "hi hello")

print("Testing rawget and rawset...")
local t8 = setmetatable({ x = 10 }, {
    __index = function() return 999 end,
    __newindex = function() end
})
assert(t8.x == 10)
assert(t8.y == 999)
assert(rawget(t8, "x") == 10)
assert(rawget(t8, "y") == nil)

t8.z = 123
assert(t8.z == 999) -- intercepted by __index
assert(rawget(t8, "z") == nil)

rawset(t8, "z", 456)
assert(rawget(t8, "z") == 456)
assert(t8.z == 456)

print("Metatable tests passed!")
