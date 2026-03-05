print("Testing uservalue...")
local f = io.tmpfile()
if type(f) == "userdata" then
    print(debug.getuservalue(f))
end
print("OK")
