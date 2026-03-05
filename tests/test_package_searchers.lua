-- Test package.searchers

print("Testing package.preload...")
package.preload["test_mod"] = function(name)
    return { name = name, preloaded = true }
end

local m = require("test_mod")
assert(m.name == "test_mod")
assert(m.preloaded == true)
print("OK")

print("Testing custom searcher...")
table.insert(package.searchers, function(name)
    if name == "custom" then
        return function(modname)
            return { name = modname, custom = true }
        end
    end
    return "no custom searcher for " .. name
end)

local m2 = require("custom")
assert(m2.name == "custom")
assert(m2.custom == true)
print("OK")

print("All package.searchers tests passed!")
