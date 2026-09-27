-- Targeted tests for previously uncovered opcodes.
-- luacheck: ignore

-- OP_POW
assert(2^3 == 8)
assert(4^0.5 == 2)
assert(2^-1 == 0.5)

-- OP_DEF_GLOBAL / OP_DEF_GLOBAL_TABLE (plain global assignment, not via _G)
test_global_cov = 42
assert(test_global_cov == 42)
test_global_cov = nil

-- OP_YIELD (coroutine yield)
local co = coroutine.create(function()
  coroutine.yield(1)
  coroutine.yield(2)
  return 3
end)
local _, v1 = coroutine.resume(co)
assert(v1 == 1)
local _, v2 = coroutine.resume(co)
assert(v2 == 2)
local _, v3 = coroutine.resume(co)
assert(v3 == 3)

-- OP_GET_VARARG_COUNT / OP_GET_VARARG_ITEM / OP_PACK_VARARG_TABLE
local function varargs(...)
  local n = select("#", ...)
  assert(n == 3)
  local t = {...}
  assert(#t == 3 and t[1] == 1 and t[2] == 2 and t[3] == 3)
  return n
end
assert(varargs(1, 2, 3) == 3)

-- OP_CLOSURE_LONG / OP_CONSTANT_LONG
-- (need >255 constants to trigger long variants)
local parts = {"local t = {"}
for i = 1, 300 do
  parts[#parts + 1] = string.format("%d,", i)
end
parts[#parts + 1] = "}"
parts[#parts + 1] = "return t[300]"
local chunk = table.concat(parts, "\n")
local fn = assert(load(chunk))
assert(fn() == 300)

print("opcode coverage tests passed")
