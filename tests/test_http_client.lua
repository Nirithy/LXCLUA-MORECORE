local http = require("http")

local host = "127.0.0.1"
local port = 8080

print("Connecting to " .. host .. ":" .. port)
-- Note: libhttp.c uses gethostbyname, so "localhost" or "127.0.0.1" should work.
local client = http.client(host, port)

if not client then
    print("Failed to connect")
    os.exit(1)
end

local req = "GET / HTTP/1.0\r\nHost: " .. host .. "\r\n\r\n"
client:send(req)

local res = client:recv(4096)
if res then
    print("Received response: " .. #res .. " bytes")
    print(res)
else
    print("No response received")
end

client:close()
