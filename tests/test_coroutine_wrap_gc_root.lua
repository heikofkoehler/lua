-- Regression test: coroutine.wrap/coroutine.create must keep the new coroutine
-- rooted across internal allocations (compileSource/createClosure) that can
-- trigger GC. Previously, native_coroutine_wrap popped the coroutine into a
-- C++ local, then compileSource ran a GC that collected it; the dangling
-- coroutine (and its frame closure) later crashed coroutine.resume with a
-- null FunctionObject* (SIGSEGV in FunctionObject::arity()).

local function stress_wrap(n)
    for i = 1, n do
        -- Allocate garbage to keep GC pressure high so a collection is likely
        -- during the wrap() internal compileSource.
        local trash = {}
        for j = 1, 50 do trash[j] = "pad" .. j .. string.rep("x", 10) end
        local f = coroutine.wrap(function(x) return x * 2 end)
        -- The wrapped coroutine must survive any GC triggered inside wrap().
        assert(f(21) == 42, "wrap result wrong at iteration " .. i)
        if i % 50 == 0 then collectgarbage("collect") end
    end
end

local function stress_create(n)
    for i = 1, n do
        local fn = function(x) return x + 1 end
        local co = coroutine.create(fn)
        if i % 50 == 0 then collectgarbage("collect") end
        local ok, v = coroutine.resume(co, 41)
        assert(ok and v == 42, "create/resume wrong at iteration " .. i)
    end
end

stress_wrap(2000)
stress_create(2000)
-- Also exercise wrap under generational GC if available.
if collectgarbage("generational") then
    stress_wrap(500)
    stress_create(500)
    collectgarbage("incremental")
end

print("test_coroutine_wrap_gc_root: OK")
