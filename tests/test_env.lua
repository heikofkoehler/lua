-- Test _ENV support
print("=== Default _ENV ===")
x = 10
print("x =", x)
print("_ENV.x =", _ENV.x)

print("
=== Custom _ENV ===")
local my_env = {
    print = print,
    y = 20
}

do
    local _ENV = my_env
    print("Inside custom _ENV")
    print("y =", y)
    z = 30
    print("z =", z)
end

print("
=== Back to default _ENV ===")
print("x =", x)
print("y =", y) -- should be nil
print("z =", z) -- should be nil
print("my_env.z =", my_env.z) -- should be 30
