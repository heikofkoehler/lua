-- Regression test: table.unpack must not hang (or overflow) when the range
-- touches integer extremes. Previously `for (k = i; k <= j; k++)` with
-- j == INT64_MAX wrapped around and looped ~2^64 times (effectively a hang
-- followed by OOM).
--
-- luacheck: ignore

local maxI = math.maxinteger
local minI = math.mininteger

-- Range ending at maxinteger: the terminal increment must not wrap.
do
    local t = { [maxI - 1] = 12, [maxI] = 23 }
    local a, b = table.unpack(t, maxI - 1, maxI)
    assert(a == 12 and b == 23, "unpack at maxinteger range failed")
end

-- Single element at maxinteger.
do
    local t = { [maxI] = 42 }
    local a = table.unpack(t, maxI, maxI)
    assert(a == 42, "unpack single at maxinteger failed")
end

-- Range starting at mininteger.
do
    local t = { [minI] = 7, [minI + 1] = 8 }
    local a, b = table.unpack(t, minI, minI + 1)
    assert(a == 7 and b == 8, "unpack at mininteger range failed")
end

-- Empty (reversed) ranges at both extremes must return nothing, quickly.
do
    local t = { [maxI] = 1, [minI] = 2 }
    local n = select("#", table.unpack(t, maxI, maxI - 1))
    assert(n == 0, "reversed range at maxinteger should be empty")
    n = select("#", table.unpack(t, minI + 1, minI))
    assert(n == 0, "reversed range at mininteger should be empty")
end

-- Default range still works (regression sanity).
do
    local a, b, c = table.unpack({ 10, 20, 30 })
    assert(a == 10 and b == 20 and c == 30, "default unpack range broken")
end

print("test_table_unpack_extreme: all assertions passed")
