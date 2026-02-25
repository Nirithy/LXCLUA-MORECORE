local tcc = require("tcc")

local function compile_and_load(name, code)
    print("---------------------------------------------------")
    print("Test Case: " .. name)

    -- Generate C code
    local c_code = tcc.compile(code, name)
    -- print("Generated C Code:\n" .. c_code) -- Debug

    local c_file = name .. ".c"
    local so_file = name .. ".so"

    local f = io.open(c_file, "w")
    f:write(c_code)
    f:close()

    -- Compile
    -- Use -I. to find lua.h in current dir
    -- Use -undefined dynamic_lookup on macOS if needed, but here assuming Linux
    local cmd = string.format("gcc -std=c99 -shared -o %s %s -I. -fPIC", so_file, c_file)
    print("Executing: " .. cmd)
    local ret = os.execute(cmd)
    if ret ~= 0 and ret ~= true then
        error("GCC compilation failed for " .. name)
    end

    -- Clear package.loaded to force reload if needed
    if package.loaded[name] then
        package.loaded[name] = nil
    end

    -- Load
    local old_path = package.cpath
    package.cpath = "./?.so;" .. old_path
    local ok, mod = pcall(require, name)
    package.cpath = old_path

    if not ok then
        error("Failed to require module " .. name .. ": " .. tostring(mod))
    end

    print("Module " .. name .. " loaded successfully.")
    return mod
end

-- Test 1: Simple Print (Side effect verification is hard, so we just run it)
compile_and_load("test_print", [[
    print("Hello from TCC Runtime Test")
]])

-- Test 2: Return value arithmetic
local mod_math = compile_and_load("test_math", [[
    return 10 + 20
]])
-- require returns the result of the main chunk if it returns something?
-- In generated C: luaopen_... calls the function.
-- If the function returns values, luaopen_... usually ignores them unless it pushes them?
-- Generated code:
-- luaopen_...:
--   lua_call(L, 0, 1);
--   return 1;
-- If function returns 1 value, it stays on stack. luaopen returns 1.
-- So require returns it.
if mod_math ~= 30 then
    error("Test 2 Failed: Expected 30, got " .. tostring(mod_math))
end
print("Test 2 Passed: 10 + 20 = " .. tostring(mod_math))

-- Test 3: Local variables
local mod_local = compile_and_load("test_local", [[
    local x = 5
    local y = 6
    return x * y
]])
if mod_local ~= 30 then
    error("Test 3 Failed: Expected 30, got " .. tostring(mod_local))
end
print("Test 3 Passed: 5 * 6 = " .. tostring(mod_local))

-- Test 4: Global Variable access
_G.TEST_GLOBAL = 100
local mod_global = compile_and_load("test_global", [[
    return TEST_GLOBAL + 1
]])
if mod_global ~= 101 then
    error("Test 4 Failed: Expected 101, got " .. tostring(mod_global))
end
print("Test 4 Passed: Global access works")

-- Test 5: Table creation and access
local mod_table = compile_and_load("test_table", [[
    local t = { a = 42 }
    return t.a
]])
if mod_table ~= 42 then
    error("Test 5 Failed: Expected 42, got " .. tostring(mod_table))
end
print("Test 5 Passed: Table works")

-- Test 6: Control Flow
local mod_flow = compile_and_load("test_flow", [[
    local x = 10
    if x > 5 then
        return "greater"
    else
        return "smaller"
    end
]])
if mod_flow ~= "greater" then
    error("Test 6 Failed: Expected 'greater', got " .. tostring(mod_flow))
end
print("Test 6 Passed: Control flow works")

-- Clean up
os.execute("rm test_print.c test_print.so")
os.execute("rm test_math.c test_math.so")
os.execute("rm test_local.c test_local.so")
os.execute("rm test_global.c test_global.so")
os.execute("rm test_table.c test_table.so")
os.execute("rm test_flow.c test_flow.so")

print("---------------------------------------------------")
print("ALL RUNTIME TESTS PASSED")
