-- Test coroutine.close

print("Testing coroutine.close...")
local co = coroutine.create(function()
    coroutine.yield()
end)

assert(coroutine.status(co) == "suspended")
local ok, err = coroutine.close(co)
assert(ok)
assert(coroutine.status(co) == "dead")
print("OK")

print("All coroutine.close tests passed!")
