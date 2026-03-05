-- Complete String Library Tests
print("Testing String Library...")

-- Basic functions
assert(string.len("hello") == 5)
assert(string.sub("hello", 2, 4) == "ell")
assert(string.sub("hello", -3) == "llo")
assert(string.upper("hello") == "HELLO")
assert(string.lower("WORLD") == "world")
assert(string.reverse("abc") == "cba")
assert(string.byte("ABC", 2) == 66)
assert(string.char(65, 66) == "AB")

-- Pattern matching
assert(string.find("hello world", "world") == 7)
assert(string.match("hello 123 world", "%d+") == "123")
local s, count = string.gsub("hello world", "o", "0")
assert(s == "hell0 w0rld")
assert(count == 2)

-- Format
assert(string.format("hello %s", "world") == "hello world")
assert(string.format("num: %d, hex: %x", 42, 42) == "num: 42, hex: 2a")

-- Repetition
assert(string.rep("a", 3) == "aaa")
assert(string.rep("b", 3, "-") == "b-b-b")

-- Packing
local data = string.pack("bi", 0x41, 12345)
assert(#data == 9)
local b, i, next_pos = string.unpack("bi", data)
assert(b == 0x41)
assert(i == 12345)
assert(next_pos == 10)
assert(string.packsize("bi") == 9)

-- Dump
local d = string.dump(function() return 42 end)
assert(type(d) == "string")
assert(string.sub(d, 1, 4) == "\x1bLua")

print("String Library Tests Passed!")
