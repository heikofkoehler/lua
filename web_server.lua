-- Simple Lua Web Server
print("Starting Lua Web Server on port 8080...")

local s = socket.create()
if not s then
    print("Failed to create socket")
    return
end

if not socket.bind(s, "127.0.0.1", 8080) then
    print("Failed to bind to port 8080")
    return
end

if not socket.listen(s, 10) then
    print("Failed to listen")
    return
end

print("Server is listening on http://127.0.0.1:8080")

-- Declare client outside the loop to avoid local scoping issues
local client

while true do
    client = socket.accept(s)
    if client then
        print("New connection!")

        -- Receive request (read first 1024 bytes)
        local request = socket.receive(client, 1024)
        if request then
            print("Request received:")

            -- Send HTTP response (using CRLF for protocol compliance)
            local response = "HTTP/1.1 200 OK\r\n" ..
                             "Content-Type: text/html\r\n" ..
                             "Content-Length: 50\r\n" ..
                             "Connection: close\r\n" ..
                             "\r\n" ..
                             "<html><body><h1>Hello from Lua!</h1></body></html>"

            socket.send(client, response)
            sleep(0.1) -- Longer sleep to ensure data is sent
        end

        socket.close(client)
        print("Connection closed")
    end

    -- Small sleep to prevent 100% CPU in case accept is non-blocking (though here it is blocking)
    sleep(0.01)
end
