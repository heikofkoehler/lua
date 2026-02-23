-- mymodule.lua
local M = {}
function hello(name)
    return "Hello, " .. name
end
M.hello = hello
M.value = 42
return M
