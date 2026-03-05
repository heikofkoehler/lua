-- Test warn functionality
print("Testing warn...")

-- 1. Basic warn
warn("this is a ", "warning")
-- Should print: Lua warning: this is a warning

-- 2. Warn off
warn("@off")
warn("this should NOT be printed")

-- 3. Warn on
warn("@on")
warn("this SHOULD be printed again")

print("OK: warn tests completed (check stderr for warnings)")
