-- Test utf8.charpattern with pattern matching functions (gmatch, gsub, match)

print("=== Testing utf8.charpattern with pattern matching ===")

-- 1. string.match with utf8.charpattern
assert(string.match("abc", utf8.charpattern) == "a")
assert(string.match("世界", utf8.charpattern) == "世")
assert(string.match("🌍🌎", utf8.charpattern) == "🌍")

-- 2. string.gmatch with utf8.charpattern iterating all codepoints
local text = "Hello, 世界! 🌍"
local codepoints = {}
for cp in string.gmatch(text, utf8.charpattern) do
    table.insert(codepoints, cp)
end

assert(#codepoints == 12, "Should find 12 UTF-8 codepoints")
assert(codepoints[1] == "H")
assert(codepoints[2] == "e")
assert(codepoints[3] == "l")
assert(codepoints[4] == "l")
assert(codepoints[5] == "o")
assert(codepoints[6] == ",")
assert(codepoints[7] == " ")
assert(codepoints[8] == "世")
assert(codepoints[9] == "界")
assert(codepoints[10] == "!")
assert(codepoints[11] == " ")
assert(codepoints[12] == "🌍")

-- 3. Counting UTF-8 characters via gmatch and comparing with utf8.len
local count = 0
for _ in string.gmatch(text, utf8.charpattern) do
    count = count + 1
end
assert(count == utf8.len(text), "gmatch count must match utf8.len")

-- 4. string.gsub with utf8.charpattern
local replaced = string.gsub("Lua 5.5 🚀", utf8.charpattern, "[%0]")
assert(replaced == "[L][u][a][ ][5][.][5][ ][🚀]")

local replaced_cap = string.gsub("Lua 5.5 🚀", "(" .. utf8.charpattern .. ")", "[%1]")
assert(replaced_cap == "[L][u][a][ ][5][.][5][ ][🚀]")

print("utf8.charpattern pattern matching tests passed!")
