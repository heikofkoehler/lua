-- Test string escape sequences

-- Simple check: call a nil value to crash with a message if condition fails
function check(cond, msg)
    if not cond then
        local fail = nil
        fail(msg)  -- runtime error: attempt to call nil
    end
end

-- \n newline
local s1 = "hello\nworld"
check(string.byte(s1, 6) == 10, "\\n should be newline")

-- \t tab
local s2 = "hello\tworld"
check(string.byte(s2, 6) == 9, "\\t should be tab")

-- \\ backslash
local s3 = "back\\slash"
check(string.len(s3) == 10, "\\\\ should be one char")
check(string.byte(s3, 5) == 92, "backslash char code should be 92")

-- \" inside double-quoted string  (say "hi" = 8 chars, "hi" at pos 6-7)
local s4 = "say \"hi\""
check(string.len(s4) == 8, "\\\" string should be 8 chars")
check(string.sub(s4, 6, 7) == "hi", "\\\" should work in double-quoted string")

-- \' inside single-quoted string  (say 'hi' = 8 chars, hi at pos 6-7)
local s5 = 'say \'hi\''
check(string.len(s5) == 8, "\\' string should be 8 chars")
check(string.sub(s5, 6, 7) == "hi", "\\' should work in single-quoted string")

-- \r carriage return
local s6 = "a\rb"
check(string.byte(s6, 2) == 13, "\\r should be carriage return")

-- Decimal escapes \ddd
local s7 = "\65\66\67"
check(s7 == "ABC", "decimal escapes \\65\\66\\67 should be ABC")

local s8 = "\104\101\108\108\111"
check(s8 == "hello", "decimal escape for 'hello' should work")

-- Hex escapes \xXX
local s9 = "\x41\x42\x43"
check(s9 == "ABC", "hex escapes \\x41\\x42\\x43 should be ABC")

local s10 = "\x68\x65\x6c\x6c\x6f"
check(s10 == "hello", "hex escape for 'hello' should work")

-- \a \b \f \v
check(string.byte("\a") == 7,  "\\a should be 7")
check(string.byte("\b") == 8,  "\\b should be 8")
check(string.byte("\f") == 12, "\\f should be 12")
check(string.byte("\v") == 11, "\\v should be 11")

print("All escape sequence tests passed")
