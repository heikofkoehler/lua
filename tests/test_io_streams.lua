-- Test io standard streams
print("Testing io standard streams...")

-- 1. Check types
assert(type(io.stdin) == "userdata")
assert(type(io.stdout) == "userdata")
assert(type(io.stderr) == "userdata")
print("OK: Stream types are userdata (FileObject)")

-- 2. Check methods on stdout
assert(io.stdout.write ~= nil)
io.stdout:write("  - Writing to stdout via method works
")
io.stdout:flush()
print("OK: io.stdout:write() works")

-- 3. Check methods on stderr
io.stderr:write("  - Writing to stderr via method works
")
print("OK: io.stderr:write() works")

print("io streams tests passed!")
