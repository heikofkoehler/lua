-- Test length operator #
print("--- String Length ---")
print(#"hello") -- 5
print(#"")      -- 0

print("--- Table Length (Array) ---")
local t = {10, 20, 30, 40}
print(#t) -- 4

print("--- Table Length (Map with Holes) ---")
local t2 = {10, 20, 30}
t2[5] = 50
print(#t2) -- 3

print("--- Table __len Metamethod ---")
local mt = {
    __len = function(t)
        return 100
    end
}
local t3 = setmetatable({}, mt)
print(#t3) -- 100

print("--- Scalar __len Metamethod ---")
debug.setmetatable(0, {
    __len = function(n)
        return 42
    end
})
print(#123) -- 42
