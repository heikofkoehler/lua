-- Test Lua pattern matching
print("=== string.find ===")
local s = "hello world"
print(s:find("hello")) -- 1 5
print(s:find("world")) -- 7 11
print(s:find("l+"))    -- 3 4
print(s:find("h.ll.")) -- 1 5

print("=== string.match ===")
print(s:match("hello")) -- hello
print(s:match("(h.ll.)")) -- hello
print(s:match("h(e)llo")) -- e
local a, b = s:match("(hello) (world)")
print(a, b) -- hello world

print("=== string.gsub ===")
print(("hello"):gsub("l", "x")) -- hexxd 2
print(("hello"):gsub("(.)", "%1%1")) -- hheelllloo 5
print(("abc"):gsub(".", {a="1", b="2", c="3"})) -- 123 3
print(("abc"):gsub(".", function(c) return c:upper() end)) -- ABC 3

print("=== string.gmatch ===")
for word in ("hello world"):gmatch("%w+") do
    print(word)
end
-- hello
-- world

print("=== Character classes ===")
print(("abc 123"):match("%d+")) -- 123
print(("abc 123"):match("%a+")) -- abc
print(("abc 123"):match("%s+")) -- " " (space)

print("=== Sets ===")
print(("abc"):match("[bc]+")) -- bc
print(("abc"):match("[^a]+")) -- bc

print("=== Quantifiers ===")
print(("aaaaa"):match("a*")) -- aaaaa
print(("aaaaa"):match("a+")) -- aaaaa
print(("aaaaa"):match("a?")) -- a
print(("aaaaa"):match("a-")) -- "" (empty)
