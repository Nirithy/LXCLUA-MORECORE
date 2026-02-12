local ptr = require "ptr"

print("Testing pointer features...")

-- Test malloc/free
local size = 128
local p = ptr.malloc(size)
assert(p, "malloc failed")
assert(not ptr.is_null(p), "pointer should not be null")

-- Test write/read (basic)
p:write("int", 12345)
assert(p:read("int") == 12345, "read/write int failed")

p:write("float", 3.14)
-- Fuzzy comparison for float
local f = p:read("float")
assert(math.abs(f - 3.14) < 0.001, "read/write float failed")

-- Test offset access (get/set)
-- int is 4 bytes usually
p:set(0, "int", 10)
p:set(4, "int", 20)
assert(p:get(0, "int") == 10, "get/set offset 0 failed")
assert(p:get(4, "int") == 20, "get/set offset 4 failed")

-- Test pointer arithmetic
local p2 = p:add(4)
assert(p2:read("int") == 20, "add offset failed")
assert(p2:sub(p) == 4, "sub pointer failed")

local p3 = p:inc(4)
assert(p3:addr() == p2:addr(), "inc failed")
local p4 = p3:dec(4)
assert(p4:addr() == p:addr(), "dec failed")

-- Test string conversion
local str = "Hello World"
local p_str = ptr.of(str)
assert(p_str:string(#str) == str, "string conversion failed")

-- Test copy/fill
p:fill(0, size) -- Zero out
assert(p:read("int") == 0, "fill failed")

local src = ptr.of("ABC")
p:copy(src, 3)
assert(p:string(3) == "ABC", "copy failed")

-- Test compare
assert(p:compare(src, 3) == 0, "compare failed")

-- Test hex
print("Hex dump:", p:tohex(4))

-- Test null
local null_p = ptr.null()
assert(ptr.is_null(null_p), "null check failed")

-- Test equality
assert(ptr.equal(p, p), "equal failed (same)")
assert(not ptr.equal(p, null_p), "equal failed (diff)")

-- Test method call syntax on pointer (via metatable)
assert(p.read == ptr.read, "metatable __index lookup failed")
assert(p:read("byte") == string.byte("A"), "method call failed")

-- Cleanup
ptr.free(p)

print("All pointer tests passed!")
