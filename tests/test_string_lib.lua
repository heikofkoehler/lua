print("=== String Library Tests ===")

-- string.len
print("string.len tests:")
print(string.len("hello"))  -- 5
print(string.len(""))  -- 0

-- string.sub
print("\nstring.sub tests:")
print(string.sub("hello", 2, 4))  -- "ell"
print(string.sub("hello", -3, -1))  -- "llo"
print(string.sub("hello", 2))  -- "ello"

-- string.upper / lower
print("\nstring.upper/lower tests:")
print(string.upper("hello"))  -- "HELLO"
print(string.lower("WORLD"))  -- "world"

-- string.reverse
print("\nstring.reverse tests:")
print(string.reverse("hello"))  -- "olleh"

-- string.byte / char
print("\nstring.byte/char tests:")
print(string.byte("ABC", 2))  -- 66
print(string.char(65, 66, 67))  -- "ABC"
