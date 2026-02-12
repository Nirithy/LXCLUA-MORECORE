
local p = ptr.malloc(1024)
if not p then
    print("malloc failed")
    os.exit(1)
end

print("Testing pointer: " .. tostring(p))

-- Test tostring format
if not tostring(p):find("pointer: 0x") then
    print("Error: tostring format incorrect: " .. tostring(p))
    os.exit(1)
end

-- Test int write/read
p["int"] = 123456789
local val = p["int"]
if val ~= 123456789 then
    print("Error: int read/write failed. Expected 123456789, got " .. tostring(val))
    os.exit(1)
end

-- Test float write/read
p["float"] = 3.14
local f = p["float"]
if math.abs(f - 3.14) > 0.0001 then
    print("Error: float read/write failed. Expected 3.14, got " .. tostring(f))
    os.exit(1)
end

-- Test offset access
local p2 = p + 4
p2["byte"] = 255
if p2["byte"] ~= 255 then
    print("Error: byte read/write failed")
    os.exit(1)
end

-- Verify byte access via integer index on original pointer
-- p points to int 123456789.
-- p + 4 points to next int.
-- p[4] is the byte at offset 4.
-- We wrote 255 to p2["byte"] which is *(byte*)p2 = *(byte*)(p+4).
-- So p[4] should be 255.
if p[4] ~= 255 then
    print("Error: p[4] byte access mismatch. Expected 255, got " .. tostring(p[4]))
    os.exit(1)
end

-- Test double
p["double"] = 123.456
local d = p["double"]
if math.abs(d - 123.456) > 0.000001 then
    print("Error: double failed")
    os.exit(1)
end

-- Test cstr (pointer to string)
-- We need a pointer to hold the string address.
local pp = ptr.malloc(8) -- size of pointer
local s = "Hello Lua Pointer"
-- Write the address of string s into memory at pp
pp["cstr"] = s
-- Read it back
local s2 = pp["cstr"]
if s2 ~= s then
    print("Error: cstr read/write failed. Expected '"..s.."', got '"..tostring(s2).."'")
    os.exit(1)
end
ptr.free(pp)

-- Test ptr construction from address
local addr = ptr.addr(p)
local p3 = ptr(addr)
if p ~= p3 then
    print("Error: ptr(addr) equality failed")
    os.exit(1)
end

-- Test mixed types via offsets
-- Write int at offset 0, float at offset 4
local block = ptr.malloc(16)
block["int"] = 100
(block + 4)["float"] = 200.5

if block["int"] ~= 100 then print("Error: block int failed") os.exit(1) end
if math.abs((block+4)["float"] - 200.5) > 0.001 then print("Error: block float failed") os.exit(1) end

ptr.free(block)
ptr.free(p)

print("All pointer tests passed successfully.")
