-- Test os.setlocale
print("Testing os.setlocale...")
local loc = os.setlocale()
assert(type(loc) == "string")
print("  current default locale:", loc)

local loc_c = os.setlocale("C")
assert(loc_c == "C")

local loc_time = os.setlocale("C", "time")
assert(loc_time == "C")

-- Invalid category
local ok, err = pcall(os.setlocale, "C", "invalid")
assert(not ok)
print("  invalid category error:", err)

-- Test os.date and os.time
print("Testing os.date and os.time...")
local now = os.time()
local d = os.date("*t", now)
assert(type(d) == "table")
assert(d.year >= 2024)

-- Test os.time with table
local t = os.time{year=2026, month=3, day=5, hour=12}
assert(type(t) == "number")
local d2 = os.date("*t", t)
assert(d2.year == 2026)
assert(d2.month == 3)
assert(d2.day == 5)
assert(d2.hour == 12)

print("OK: os.date and os.time passed")
