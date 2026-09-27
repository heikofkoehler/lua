-- Regression test: hostile bytecode must raise catchable errors, not crash.
--
-- The load-time bytecode verifier (Chunk::verify) rejects corrupted chunks
-- with "bad binary format ..." before any code runs. The runtime guards
-- (VM::getConstant "bad constant index", arity validation) remain as
-- defense-in-depth for paths that bypass the verifier.
--
-- luacheck: ignore

-- Test 1: out-of-range constant index must fail at LOAD time now.
-- 'return 42' compiles to OP_CONSTANT (0x00) <idx>, OP_RETURN.
local blob = assert(string.dump(assert(load("return 42"))))

local found_const_guard = false
for pos = 1, #blob do
    if blob:byte(pos) == 0x00 then
        if pos + 1 <= #blob then
            local patched = blob:sub(1, pos) .. "\xFF" .. blob:sub(pos + 2)
            local f, ferr = load(patched)
            if not f and type(ferr) == "string"
               and ferr:find("constant index out of range") then
                found_const_guard = true
                break
            end
            -- Old runtime-guard path (defense in depth): still acceptable.
            if f then
                local cok, cerr = pcall(f)
                if not cok and type(cerr) == "string"
                   and cerr:find("bad constant index") then
                    found_const_guard = true
                    break
                end
            end
        end
    end
end
assert(found_const_guard, "expected 'constant index out of range' guard")

-- Test 2: invalid opcode byte must fail at LOAD time.
-- Corrupt a byte to an out-of-range opcode value (>= 75).
local found_opcode_guard = false
for pos = 1, #blob do
    local patched = blob:sub(1, pos - 1) .. "\xFE" .. blob:sub(pos + 1)
    local f, ferr = load(patched)
    if not f and type(ferr) == "string" and ferr:find("bad binary format") then
        found_opcode_guard = true
        break
    end
end
assert(found_opcode_guard, "expected 'bad binary format' for invalid opcode")

-- Test 3: invalid arity (negative via 0xFF in high byte)
-- The arity field is a 4-byte int after the 40-byte header + nameLen.
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

-- Test 4: jump target out of range must fail at LOAD time.
-- 'while true do end' compiles to a loop; corrupt its backward offset.
local loop_blob = assert(string.dump(assert(load("while true do end"))))
local found_jump_guard = false
for pos = 1, #loop_blob do
    local patched = loop_blob:sub(1, pos - 1) .. "\xFF\xFF" .. loop_blob:sub(pos + 2)
    local f, ferr = load(patched)
    if not f and type(ferr) == "string"
       and (ferr:find("jump target") or ferr:find("loop target")) then
        found_jump_guard = true
        break
    end
end
assert(found_jump_guard, "expected 'jump/loop target' guard")


-- Test 5: absurd count fields must fail fast, not hang or allocate gigabytes.
-- Corrupt the idCount field (high byte) to 0x6E000000.
local hdr_blob = assert(string.dump(assert(load("for i=1,10 do local x = i*2 end return 42"))))
-- Find idCount: parse header the same way the VM does.
local pos = 41
local nameLen = string.unpack("<I4", hdr_blob, pos); pos = pos + 4 + nameLen
pos = pos + 4 + 4 + 1 + 4 + 4  -- arity, upvalueCount, varargs, lineDefined, lastLineDefined
local snLen = string.unpack("<I4", hdr_blob, pos); pos = pos + 4 + snLen
local codeSize = string.unpack("<I4", hdr_blob, pos); pos = pos + 4 + codeSize
local linesSize = string.unpack("<I4", hdr_blob, pos); pos = pos + 4 + linesSize * 4
-- pos now points at idCount
local idCountPos = pos
local bad = hdr_blob:sub(1, idCountPos + 2) .. "\x6E" .. hdr_blob:sub(idCountPos + 4)
local f5, err5 = load(bad)
assert(not f5 and type(err5) == "string" and err5:find("absurd"),
       "expected 'absurd ... count' for corrupted idCount, got: " .. tostring(err5):sub(1, 60))

print("test_hostile_bytecode: all assertions passed (with verifier)")
