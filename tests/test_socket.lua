print("Testing socket library...")

local s = socket.create()
assert(type(s) == "userdata", "socket.create() should return userdata")

local ok = socket.bind(s, "127.0.0.1", 12345)
assert(ok == true, "socket.bind should succeed")

ok = socket.listen(s, 5)
assert(ok == true, "socket.listen should succeed")

-- Closing shouldn't error
socket.close(s)
print("Socket library basic tests passed!")
