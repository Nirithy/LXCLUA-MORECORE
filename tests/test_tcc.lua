local tcc = require "tcc"

local code = [[
local function add(a, b)
  return a + b
end

print("Hello from TCC compiled module!")
return add(10, 20)
]]

local modname = "test_mod_tcc"
local c_code = tcc.compile(code, modname)

print("Generated C Code length: " .. #c_code)

local f = io.open("tests/test_mod_tcc.c", "w")
f:write(c_code)
f:close()

print("Compiling C code...")
-- Assuming gcc is available and we are in root (so -I. works)
-- Use -shared -fPIC to create a shared library
local cmd = "gcc -O2 -shared -fPIC -o tests/test_mod_tcc.so tests/test_mod_tcc.c -I."
-- If on Mac, it might be .dylib and different flags, but environment seems Linux
-- If undefined symbols, we might need -llua or -undefined dynamic_lookup
-- Since lxclua exports symbols (-Wl,-E), we should be fine on Linux with -undefined dynamic_lookup equivalent or just nothing.
-- On Linux, usually fine.

local ret = os.execute(cmd)
if ret ~= true and ret ~= 0 then
    print("Compilation failed!")
    os.exit(1)
end

print("Loading compiled module...")
package.cpath = package.cpath .. ";./tests/?.so"
local res = require(modname)

print("Result: " .. tostring(res))

if res == 30 then
    print("TEST PASSED")
else
    print("TEST FAILED: Expected 30, got " .. tostring(res))
    os.exit(1)
end
