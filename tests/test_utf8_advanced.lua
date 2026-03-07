-- Advanced UTF8 Library Tests

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %s, got %s. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing UTF8 len with invalid sequences ===")
local s_invalid = "abc\xffdef"
local len, pos = utf8.len(s_invalid)
assert_eq(len, nil)
assert_eq(pos, 4)

print("=== Testing UTF8 offset with negative counts ===")
local s = "héllo"
-- s[1] = h (1)
-- s[2] = é (2, 3)
-- s[4] = l (4)
-- s[5] = l (5)
-- s[6] = o (6)
assert_eq(utf8.offset(s, 1), 1)
assert_eq(utf8.offset(s, 2), 2)
assert_eq(utf8.offset(s, 3), 4)
assert_eq(utf8.offset(s, -1), 6)
assert_eq(utf8.offset(s, -2), 5)
assert_eq(utf8.offset(s, -3), 4)
assert_eq(utf8.offset(s, -4), 2)
assert_eq(utf8.offset(s, -5), 1)

print("=== Testing UTF8 codepoint ranges ===")
local s2 = "abc"
local c1, c2, c3 = utf8.codepoint(s2, 1, 3)
assert_eq(c1, 97)
assert_eq(c2, 98)
assert_eq(c3, 99)

print("\nAdvanced UTF8 tests passed!")
