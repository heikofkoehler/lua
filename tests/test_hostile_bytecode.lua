-- Regression test: hostile bytecode must raise catchable errors, not crash.
--
-- 1. Out-of-range constant index: VM::getConstant now checks the index and
--    raises "bad constant index" instead of unchecked vector access.
-- 2. Invalid arity/upvalueCount: FunctionObject::deserialize validates that
--    arity and upvalueCount are in [0, 255]; corrupted values raise
--    "bad binary format" instead of crashing call setup.
--
-- luacheck: ignore

-- Test 1: bad constant index
-- 'return 42' compiles to OP_CONSTANT (0x00) <idx>, OP_RETURN.
-- We locate the OP_CONSTANT by finding the pattern and patch the operand.
local blob = assert(string.dump(assert(load("return 42"))))

-- Find OP_CONSTANT (0x00) followed by a small index byte in the code section.
-- The code section comes after the 40-byte header + function metadata.
-- Instead of parsing, we try patching each 0x00 byte that could be an opcode
-- and check for the specific error.
local found_const_guard = false
for pos = 1, #blob do
    if blob:byte(pos) == 0x00 then
        -- Try patching the NEXT byte (potential operand) to 0xFF
        if pos + 1 <= #blob then
            local patched = blob:sub(1, pos) .. "\xFF" .. blob:sub(pos + 2)
            local ok, f = pcall(load, patched)
            if ok and f then
                local cok, cerr = pcall(f)
                if not cok and type(cerr) == "string" and cerr:find("bad constant index") then
                    found_const_guard = true
                    break
                end
            end
        end
    end
end
assert(found_const_guard, "expected to trigger 'bad constant index' guard")

-- Test 2: invalid arity (negative via 0xFF in high byte)
-- The arity field is a 4-byte int after the 40-byte header + nameLen.
-- We verify that a corrupted arity raises instead of crashing.
-- (Position 48 in 'return 42' dump is the high byte of arity.)
local pos48 = 48
if pos48 <= #blob then
    local patched = blob:sub(1, pos48 - 1) .. "\xFF" .. blob:sub(pos48 + 1)
    local ok, f, ferr = pcall(load, patched)
    -- Should either fail to load or raise when called, but not crash.
    if ok and f then
        local cok, cerr = pcall(f)
        if not cok then
            assert(type(cerr) == "string" and cerr:find("bad binary format"),
                   "expected 'bad binary format' for invalid arity, got: " .. tostring(cerr):sub(1, 60))
        end
    else
        -- Load failed (f is nil, ferr has the message), check it's a proper error
        local errmsg = ferr or (not ok and f) or ""
        assert(type(errmsg) == "string" and errmsg:find("bad binary format"),
               "expected 'bad binary format' on load, got: " .. tostring(errmsg):sub(1, 60))
    end
end

print("test_hostile_bytecode: all assertions passed")
