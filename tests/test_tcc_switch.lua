local tcc = require("tcc")

local function compile_and_load(name, code)
    print("Compiling " .. name)
    local c_code = tcc.compile(code, name)
    local c_file = name .. ".c"
    local so_file = name .. ".so"
    local log_file = name .. ".log"

    local f = io.open(c_file, "w")
    f:write(c_code)
    f:close()

    local cmd = string.format("gcc -std=c99 -shared -o %s %s -I. -fPIC > %s 2>&1", so_file, c_file, log_file)
    local ret1, ret2, ret3 = os.execute(cmd)
    -- Handle Lua 5.1 vs 5.2+ os.execute return values
    local success = false
    if ret1 == 0 or ret1 == true then
        success = true
    end

    if not success then
        local f = io.open(log_file, "r")
        local log = f:read("*a")
        f:close()
        error("GCC compilation failed (ret="..tostring(ret1).."):\n" .. tostring(log))
    end

    local old_path = package.cpath
    package.cpath = "./?.so;" .. old_path
    local ok, mod = pcall(require, name)
    package.cpath = old_path

    if not ok then error("Require failed: " .. tostring(mod)) end
    return mod
end

-- Test Switch + Defer with TCC
-- This exercises OP_CLOSE generation in TCC
local code = [[
    return function(val)
        local log = {}
        local function log_append(msg) table.insert(log, msg) end

        switch (val) do
            case 1:
                defer do log_append("d1") end
            case 2:
                defer do log_append("d2") end
                break
        end
        return table.concat(log, ",")
    end
]]

local mod = compile_and_load("test_tcc_switch_mod", code)
local func = mod

print("Running compiled switch(1)...")
local res = func(1)
print("Result: " .. tostring(res))
if res ~= "d2,d1" then
    error("Expected 'd2,d1', got '" .. tostring(res) .. "'")
end

print("Running compiled switch(2)...")
local res2 = func(2)
print("Result: " .. tostring(res2))
if res2 ~= "d2" then
    error("Expected 'd2', got '" .. tostring(res2) .. "'")
end

print("TCC Switch Test Passed")
