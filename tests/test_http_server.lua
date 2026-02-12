local http = require("http")

local port = 8080
local server = http.server(port)

if not server then
    print("Failed to start server")
    os.exit(1)
end

print("Server listening on port " .. port)

-- Accept one connection
local client = server:accept()
if client then
    print("Client connected")
    local req = client:recv(4096)
    if req then
        print("Received request: " .. #req .. " bytes")

        local body = "<h1>Hello from XCLUA HTTP Server</h1>"
        local response = "HTTP/1.0 200 OK\r\n" ..
                         "Content-Type: text/html\r\n" ..
                         "Content-Length: " .. #body .. "\r\n" ..
                         "Connection: close\r\n" ..
                         "\r\n" ..
                         body

        local sent = client:send(response)
        print("Sent response: " .. sent .. " bytes")
    else
        print("Client closed connection or error")
    end
    client:close()
end

server:close()
print("Server closed")
