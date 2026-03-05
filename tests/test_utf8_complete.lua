-- Complete UTF8 Library Tests
print("Testing UTF8 Library...")

local s = "héllo" -- 'é' is 2 bytes in UTF-8

-- len
assert(utf8.len(s) == 5)
assert(utf8.len("abc") == 3)

-- codepoint
local cp = utf8.codepoint(s, 2)
assert(cp == 233) -- é

-- char
assert(utf8.char(233) == "é")
assert(utf8.char(72, 105) == "Hi")

-- offset
assert(utf8.offset(s, 1) == 1)
assert(utf8.offset(s, 2) == 2)
assert(utf8.offset(s, 3) == 4) -- 'é' takes 2 bytes

-- codes
local cps = {}
for p, c in utf8.codes(s) do
    table.insert(cps, c)
end
assert(#cps == 5)
assert(cps[2] == 233)

print("UTF8 Library Tests Passed!")
