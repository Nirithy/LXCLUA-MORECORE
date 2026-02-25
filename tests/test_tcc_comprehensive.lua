local tcc = require("tcc")

local code = [[
-- Math and Logic
function test_math(a, b)
    local x = a + b
    x = x * 2
    x = x - 5
    x = x / 2
    x = x // 1
    x = x % 4
    x = x ^ 3
    x = x & 0xFF
    x = x | 0x0F
    x = x ~ 0xAA
    x = x << 1
    x = x >> 1
    return x
end

-- Comparison
function test_cmp(a, b)
    if a > b then return 1 end
    if a < b then return -1 end
    if a >= b then return 2 end
    if a <= b then return -2 end
    if a == b then return 0 end
    return 99
end

-- Varargs
function test_vararg(...)
    local a, b, c = ...
    return c, b, a
end

-- Tables
function test_table()
    local t = {10, 20, 30, x=100}
    return t[1] + t.x
end

-- Classes
class Point
    public x = 0
    public y = 0
    function __init__(self, x, y)
        self.x = x
        self.y = y
    end
    function add(self, other)
        return Point(self.x + other.x, self.y + other.y)
    end
end

class Point3D extends Point
    public z = 0
    function __init__(self, x, y, z)
        super(x, y)
        self.z = z
    end
    function get_z(self)
        return self.z
    end
end

function test_oo()
    local p1 = Point(10, 20)
    local p2 = Point(5, 5)
    local p3 = p1:add(p2)

    local p3d = Point3D(1, 2, 3)

    return p3.x + p3.y + p3d:get_z() -- (15 + 25) + 3 = 43
end

return {
    test_math = test_math,
    test_cmp = test_cmp,
    test_vararg = test_vararg,
    test_table = test_table,
    test_oo = test_oo
}
]]

local modname = "test_mod_comprehensive"
local status, c_code = pcall(tcc.compile, code, modname)

if not status then
    print("Compilation Error: " .. tostring(c_code))
    os.exit(1)
end

local f = io.open("tests/" .. modname .. ".c", "w")
f:write(c_code)
f:close()

print("Compiling C code...")
local cmd = "gcc -O2 -shared -fPIC -o tests/" .. modname .. ".so tests/" .. modname .. ".c -I."
local ret = os.execute(cmd)
if ret ~= true and ret ~= 0 then
    print("GCC Compilation failed!")
    os.exit(1)
end

print("Loading compiled module...")
package.cpath = package.cpath .. ";./tests/?.so"
local mod = require(modname)

-- Run tests
print("Running test_math...")
local res = mod.test_math(10, 20)
-- Expected: 181
if res ~= 181 then
    print("Math result: " .. tostring(res) .. " (Expected 181)")
    error("Math failed")
end
print("Math passed")

print("Running test_cmp...")
local cmp = mod.test_cmp(10, 5)
if cmp ~= 1 then error("CMP > failed") end
cmp = mod.test_cmp(5, 10)
if cmp ~= -1 then error("CMP < failed") end
print("Comparison passed")

print("Running test_vararg...")
local a, b, c = mod.test_vararg(1, 2, 3)
if a ~= 3 or b ~= 2 or c ~= 1 then error("Vararg failed: " .. tostring(a)..","..tostring(b)..","..tostring(c)) end
print("Vararg passed")

print("Running test_table...")
local tres = mod.test_table()
if tres ~= 110 then error("Table failed: " .. tostring(tres)) end
print("Table passed")

print("Running test_oo...")
local oo_res = mod.test_oo()
if oo_res ~= 43 then error("OO failed: " .. tostring(oo_res)) end
print("OO passed")

print("ALL TESTS PASSED")
