-- Advanced string.format tests

local function assert_eq(actual, expected, msg)
    if actual ~= expected then
        error(string.format("Assertion failed: expected %q, got %q. %s", tostring(expected), tostring(actual), msg or ""), 2)
    end
end

print("=== Testing string.format widths and precision ===")
assert_eq(string.format("%5s", "foo"), "  foo")
assert_eq(string.format("%-5s", "foo"), "foo  ")
assert_eq(string.format("%.2f", 1.2345), "1.23")
assert_eq(string.format("%04d", 42), "0042")
assert_eq(string.format("%x", 255), "ff")
assert_eq(string.format("%X", 255), "FF")

print("=== Testing string.format %q corner cases ===")
assert_eq(string.format("%q", "a\nb"), [["a\nb"]])
assert_eq(string.format("%q", 'a"b'), [["a\"b"]])
assert_eq(string.format("%q", "a\\b"), [["a\\b"]])
-- Lua 5.4 %q handles nil, booleans, and numbers
assert_eq(string.format("%q", nil), "nil")
assert_eq(string.format("%q", true), "true")
assert_eq(string.format("%q", 42), "42")

print("=== Testing string.format multiple arguments ===")
assert_eq(string.format("%s %d %s", "one", 2, "three"), "one 2 three")

print("\nString format tests passed!")
