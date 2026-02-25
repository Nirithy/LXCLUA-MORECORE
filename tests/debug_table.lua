local tcc = require("tcc")

local code = [[
local function test_table_debug()
    local t = {10, 20, 30, x=100}
    print("t[1]=" .. tostring(t[1]))
    print("t.x=" .. tostring(t.x))
    return t[1] + t.x
end
return { test_table_debug = test_table_debug }
]]

local modname = "test_table_debug"
local c_code = tcc.compile(code, modname)
local f = io.open("tests/" .. modname .. ".c", "w")
f:write(c_code)
f:close()

os.execute("gcc -O2 -shared -fPIC -o tests/" .. modname .. ".so tests/" .. modname .. ".c -I.")
package.cpath = package.cpath .. ";./tests/?.so"
local mod = require(modname)
mod.test_table_debug()
