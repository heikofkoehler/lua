-- Comprehensive Lua Pattern Matching Tests

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %q, got %q. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing Pattern Classes ===")
assert_eq(string.match("abc123XYZ", "%d+"), "123")
assert_eq(string.match("abc123XYZ", "%a+"), "abc")
assert_eq(string.match("abc 123", "%s"), " ")
assert_eq(string.match("abc.123", "%p"), ".")
assert_eq(string.match("abc\n123", "%c"), "\n")
assert_eq(string.match("abc123XYZ", "%l+"), "abc")
assert_eq(string.match("abc123XYZ", "%u+"), "XYZ")
assert_eq(string.match("abc123XYZ", "%w+"), "abc123XYZ")
assert_eq(string.match("FF", "%x+"), "FF")

print("=== Testing Negated Classes ===")
assert_eq(string.match("123abc456", "%D+"), "abc")
assert_eq(string.match("abc 123", "%S+"), "abc")

print("=== Testing Sets and Ranges ===")
assert_eq(string.match("hello", "[aeiou]+"), "e")
assert_eq(string.match("123abc456", "[a-z]+"), "abc")
assert_eq(string.match("123ABC456", "[A-Z]+"), "ABC")
assert_eq(string.match("123-456", "[0-9%-]+"), "123-456")
assert_eq(string.match("hello", "[^aeiou]+"), "h")

print("=== Testing Quantifiers ===")
assert_eq(string.match("aaaaa", "a*"), "aaaaa")
assert_eq(string.match("aaaaa", "a+"), "aaaaa")
assert_eq(string.match("aaaaa", "a?"), "a")
assert_eq(string.match("aaaaa", "a-"), "")
assert_eq(string.match("baaaaa", "ba-"), "b")
assert_eq(string.match("baaaaac", "ba-c"), "baaaaac")

print("=== Testing Anchors ===")
assert_eq(string.match("hello", "^h"), "h")
assert_eq(string.match("hello", "o$"), "o")
assert_eq(string.match("hello", "^e"), nil)
assert_eq(string.match("hello", "l$"), nil)

print("=== Testing Captures ===")
local a, b = string.match("hello world", "(%a+)%s+(%a+)")
assert_eq(a, "hello")
assert_eq(b, "world")

local s = "name = value"
local k, v = string.match(s, "(%w+)%s*=%s*(%w+)")
assert_eq(k, "name")
assert_eq(v, "value")

print("=== Testing Nested Captures ===")
local a, b = string.match("hello", "((he)llo)")
assert_eq(a, "hello")
assert_eq(b, "he")

print("=== Testing Back-references ===")
assert_eq(string.match("abcabc", "(...)%1"), "abc")
assert_eq(string.match("aabb", "(.)%1(.)%2"), "a") -- first capture returned by string.match

print("=== Testing Position Captures ===")
local b, e, p1, p2 = string.find("hello", "()el()")
assert_eq(b, 2)
assert_eq(e, 3)
assert_eq(p1, 2)
assert_eq(p2, 4)

print("=== Testing Balanced Strings ===")
assert_eq(string.match("a (b (c) d) e", "%b()"), "(b (c) d)")
assert_eq(string.match("a [b [c] d] e", "%b[]"), "[b [c] d]")

print("=== Testing string.gsub with Captures ===")
assert_eq(string.gsub("hello world", "(%w+)", "%1 %1"), "hello hello world world", "gsub %1")
assert_eq(string.gsub("hello world", "%w+", function(s) return s:upper() end), "HELLO WORLD", "gsub func")
assert_eq(string.gsub("a=b, c=d", "(%w+)=(%w+)", {a="1", c="2"}), "1, 2", "gsub table")

print("=== Testing Frontier Patterns ===")
-- %f[set] matches an empty string at a position such that 
-- the next character belongs to set and the previous character does not
assert_eq(string.gsub("them cat in the hat", "%f[%w]the%f[%W]", "THE"), "them cat in THE hat") -- only standalone "the"
-- Wait, standard Lua: "the" is at start, so prev is non-word. next is space, so non-word.
-- Actually:
-- Position 1: prev=nil (non-word), curr='t' (word). Matches %f[%w].
-- Position 4: prev='e' (word), curr=' ' (non-word). Matches %f[%W].
-- So "the" at start SHOULD match.
-- Let's re-verify:
-- "the cat"
-- ^ (pos 1) -> prev=nil, next='t'. %f[%w] matches.
--    ^ (pos 4) -> prev='e', next=' '. %f[%W] matches.
assert_eq(string.match("hello", "%f[%l]h"), "h")

print("\nAll pattern matching tests passed!")
