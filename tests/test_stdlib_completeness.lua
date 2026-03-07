-- Comprehensive Standard Library Completeness Test
-- Matches Lua 5.4 / 5.5 (dev) specifications

local failures = 0
local function test(name, cond)
    if cond then
        print("✓ " .. name)
    else
        print("✗ " .. name)
        failures = failures + 1
    end
end

local function exists(name, func)
    test(name .. " exists", type(func) == "function" or type(func) == "table" or type(func) == "userdata" or type(func) == "string" or type(func) == "number")
end

print("=== Testing Base Library ===")
local base_funcs = {
    "assert", "collectgarbage", "dofile", "error", "getmetatable",
    "ipairs", "load", "loadfile", "next", "pairs", "pcall",
    "print", "rawequal", "rawget", "rawlen", "rawset",
    "select", "setmetatable", "tonumber", "tostring",
    "type", "_VERSION", "warn", "xpcall"
}
for _, f in ipairs(base_funcs) do exists(f, _G[f]) end

print("\n=== Testing Coroutine Library ===")
local coro_funcs = {
    "create", "isyieldable", "close", "resume", "running", "status", "wrap", "yield"
}
for _, f in ipairs(coro_funcs) do exists("coroutine." .. f, coroutine[f]) end

print("\n=== Testing Package Library ===")
local pkg_funcs = {
    "config", "cpath", "loaded", "loadlib", "path", "preload", "searchers", "searchpath"
}
for _, f in ipairs(pkg_funcs) do exists("package." .. f, package[f]) end

print("\n=== Testing String Library ===")
local str_funcs = {
    "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len",
    "lower", "match", "pack", "packsize", "rep", "reverse", "sub", "unpack", "upper"
}
for _, f in ipairs(str_funcs) do exists("string." .. f, string[f]) end

print("\n=== Testing UTF8 Library ===")
local utf8_funcs = {
    "char", "charpattern", "codes", "codepoint", "len", "offset"
}
for _, f in ipairs(utf8_funcs) do exists("utf8." .. f, utf8[f]) end

print("\n=== Testing Table Library ===")
local tbl_funcs = {
    "concat", "insert", "move", "pack", "remove", "sort", "unpack"
}
for _, f in ipairs(tbl_funcs) do exists("table." .. f, table[f]) end

print("\n=== Testing Math Library ===")
local math_funcs = {
    "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "floor",
    "fmod", "huge", "log", "max", "maxinteger", "min", "mininteger",
    "modf", "pi", "rad", "random", "randomseed", "sin", "sqrt", "tan",
    "tointeger", "type", "ult"
}
for _, f in ipairs(math_funcs) do exists("math." .. f, math[f]) end

print("\n=== Testing IO Library ===")
local io_funcs = {
    "close", "flush", "input", "lines", "open", "output", "popen",
    "read", "stderr", "stdin", "stdout", "tmpfile", "type", "write"
}
for _, f in ipairs(io_funcs) do exists("io." .. f, io[f]) end

print("\n=== Testing File Methods ===")
local f = io.tmpfile()
local file_methods = {
    "close", "flush", "lines", "read", "seek", "setvbuf", "write"
}
for _, m in ipairs(file_methods) do exists("file:" .. m, f[m]) end
f:close()

print("\n=== Testing OS Library ===")
local os_funcs = {
    "clock", "date", "difftime", "execute", "exit", "getenv",
    "remove", "rename", "setlocale", "time", "tmpname"
}
for _, f in ipairs(os_funcs) do exists("os." .. f, os[f]) end

print("\n=== Testing Debug Library ===")
local dbg_funcs = {
    "gethook", "getinfo", "getlocal", "getmetatable", "getregistry",
    "getupvalue", "getuservalue", "sethook", "setlocal", "setmetatable",
    "setupvalue", "setuservalue", "traceback", "upvalueid", "upvaluejoin"
}
for _, f in ipairs(dbg_funcs) do exists("debug." .. f, debug[f]) end

if failures > 0 then
    print("\nTOTAL FAILURES: " .. failures)
    os.exit(1)
else
    print("\nALL FUNCTIONS PRESENT!")
end
