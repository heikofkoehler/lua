print("=== String Library Tests (bracket notation) ===")

-- string.len
print("string.len:")
local len = string["len"]
print(len("hello"))  -- 5
print(len(""))  -- 0

-- string.upper / lower
print("\nstring.upper/lower:")
local upper = string["upper"]
local lower = string["lower"]
print(upper("hello"))  -- "HELLO"
print(lower("WORLD"))  -- "world"

-- string.reverse
print("\nstring.reverse:")
local reverse = string["reverse"]
print(reverse("hello"))  -- "olleh"

-- string.sub
print("\nstring.sub:")
local sub = string["sub"]
print(sub("hello", 2, 4))  -- "ell"
print(sub("hello", 2))  -- "ello"

-- string.byte / char
print("\nstring.byte/char:")
local byte = string["byte"]
local char = string["char"]
print(byte("ABC", 2))  -- 66
print(char(65, 66, 67))  -- "ABC"
