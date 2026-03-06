-- Test type metatables (debug.setmetatable)
print("Testing type metatables...")

local mt = {
    __index = {
        shout = function(self)
            return string.upper(self) .. "!!!"
        end
    }
}

debug.setmetatable("", mt)

local s = "hello"
print("shouting: " .. s:shout())
assert(s:shout() == "HELLO!!!")

-- Test for numbers
local mt_num = {
    __index = {
        square = function(self)
            return self * self
        end
    }
}

debug.setmetatable(0, mt_num)
local n = 5
print("squaring: " .. n:square())
assert(n:square() == 25)

print("OK: type metatables passed!")
