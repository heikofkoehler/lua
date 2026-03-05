-- Test debug.traceback

print("Testing debug.traceback...")
local function a()
    local function b()
        local tb = debug.traceback("hello", 1)
        print(tb)
        assert(string.find(tb, "hello"))
        assert(string.find(tb, "stack traceback:"))
        assert(string.find(tb, "test_debug_traceback.lua:%d+: in function 'b'"))
        assert(string.find(tb, "test_debug_traceback.lua:%d+: in function 'a'"))
    end
    b()
end

a()
print("OK")

print("All debug.traceback tests passed!")
