-- Test utf8 library

print("Testing utf8.char...")
assert(utf8.char(65, 66, 67) == "ABC")
assert(utf8.char(0x6C34) == "\230\176\180") -- 水
print("OK")

print("Testing utf8.len...")
assert(utf8.len("ABC") == 3)
assert(utf8.len("\230\176\180") == 1)
assert(utf8.len("Lua\230\176\180") == 4)
assert(utf8.len("abc", 2, 2) == 1)
print("OK")

print("Testing utf8.codepoint...")
assert(utf8.codepoint("ABC") == 65)
local a, b, c = utf8.codepoint("ABC", 1, 3)
assert(a == 65 and b == 66 and c == 67)
assert(utf8.codepoint("\230\176\180") == 0x6C34)
print("OK")

print("Testing utf8.offset...")
assert(utf8.offset("ABC", 1) == 1)
assert(utf8.offset("ABC", 2) == 2)
assert(utf8.offset("\230\176\180", 1) == 1)
assert(utf8.offset("\230\176\180ABC", 2) == 4)
print("OK")

print("Testing utf8.codes...")
local s = "A\230\176\180B"
local result = {}
for p, c in utf8.codes(s) do
    table.insert(result, {p, c})
end
assert(#result == 3)
assert(result[1][1] == 1 and result[1][2] == 65)
assert(result[2][1] == 2 and result[2][2] == 0x6C34)
assert(result[3][1] == 5 and result[3][2] == 66)
print("OK")

print("Testing utf8.charpattern...")
assert(type(utf8.charpattern) == "string")
assert(#utf8.charpattern > 5) -- Check it's not truncated at first \0
print("OK")

print("All utf8 tests passed!")
