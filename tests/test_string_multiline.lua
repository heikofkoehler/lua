-- Test multiline strings and comments with nesting levels
print("Testing multiline brackets...")

local s1 = [[basic long string]]
assert(s1 == "basic long string")

local s2 = [=[long string level 1 with [[nested]] brackets]=]
assert(s2 == "long string level 1 with [[nested]] brackets")

local s3 = [==[level 2 [=[ level 1 ]=] level 2]==]
assert(s3 == "level 2 [=[ level 1 ]=] level 2")

-- Multiline comments
--[[
  basic multiline comment
]]

--[=[
  level 1 comment with --[[ nested ]] comment
]=]

print("OK: multiline brackets passed!")
