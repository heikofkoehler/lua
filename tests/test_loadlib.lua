print("Testing package.loadlib...")

-- On macOS it might be .dylib or .so depending on how CMake builds it.
-- Our CMakeLists.txt outputs it as dummy_module.dylib on macOS usually, or dummy_module.so on Linux.
local function try_load(libnames, funcname)
    local f, err1, err2
    for _, name in ipairs(libnames) do
        f, err1, err2 = package.loadlib(name, funcname)
        if f then return f end
    end
    return nil, err1, err2
end

-- Since tests run from the tests/ dir and the build dir is ../build, we'll look there.
local libnames = {
    "../build/dummy_module.so",
    "../build/dummy_module.dylib",
    "../build/dummy_module.dll",
    "./build/dummy_module.so",
    "./build/dummy_module.dylib",
    "./build/dummy_module.dll",
    "dummy_module.so",
    "dummy_module.dylib",
    "dummy_module.dll"
}

local f, err1, err2 = try_load(libnames, "dummy_test_function")

if not f then
    print("FAIL: Could not load dummy_module. Error: " .. tostring(err1) .. " (" .. tostring(err2) .. ")")
    -- We don't fail the test suite entirely if the shared library isn't built or found correctly, 
    -- as dynamic loading can be platform-specific. But we should ideally find it.
    os.exit(1)
end

assert(type(f) == "function", "loadlib should return a function")
local res = f()
assert(res == 42, "dummy_test_function should return 42")

print("package.loadlib tests passed!")
