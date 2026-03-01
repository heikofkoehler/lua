-- Test invalid goto
local code = "local function test_invalid_goto()\n" ..
             "    goto skip\n" ..
             "    local a = 1\n" ..
             "    ::skip::\n" ..
             "    return a\n" ..
             "end\n"

local fn, err = load(code)

if fn then
    print("FAIL: Expected invalid goto to be rejected!")
    os.exit(1)
else
    print("Invalid goto correctly rejected:")
    print(err)
end
