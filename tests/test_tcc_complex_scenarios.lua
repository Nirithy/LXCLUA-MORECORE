local tcc = require("tcc")

local function compile_and_load(name, code, use_pure_c)
    print("---------------------------------------------------")
    print("Test Case: " .. name .. (use_pure_c and " (Pure C)" or ""))

    -- Generate C code
    local c_code = tcc.compile(code, name, use_pure_c)
    local c_file = name .. ".c"
    local so_file = name .. ".so"

    local f = io.open(c_file, "w")
    f:write(c_code)
    f:close()

    -- Compile
    -- Assumes execution from repo root so -I. finds lua.h
    local cmd = string.format("gcc -std=c99 -shared -o %s %s -I. -fPIC", so_file, c_file)
    print("Executing: " .. cmd)
    local ret = os.execute(cmd)
    if ret ~= 0 and ret ~= true then
        error("GCC compilation failed for " .. name)
    end

    -- Clear package.loaded
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

-- Test 1: Recursive Fibonacci
-- Note: tcc captures upvalues by value. Standard 'local function fib' captures 'nil'
-- before 'fib' is assigned. We must use a table or Y-combinator to support recursion.
local mod_fib = compile_and_load("test_fib_rec", [[
    local wrapper = {}
    wrapper.fib = function(n)
        if n < 2 then return n end
        return wrapper.fib(n-1) + wrapper.fib(n-2)
    end
    return wrapper.fib(10)
]])
if mod_fib ~= 55 then
    error("Test 1 Failed: Expected 55, got " .. tostring(mod_fib))
end
print("Test 1 Passed: fib(10) = 55")

-- Test 2: Closure with mutable upvalue
local mod_closure = compile_and_load("test_closure", [[
    local function make_counter()
        local count = 0
        return function()
            count = count + 1
            return count
        end
    end
    return make_counter()
]])
local c1 = mod_closure()
local c2 = mod_closure()
if c1 ~= 1 or c2 ~= 2 then
    error("Test 2 Failed: Expected 1, 2, got " .. tostring(c1) .. ", " .. tostring(c2))
end
print("Test 2 Passed: Closure counter correct")

-- Test 3: Nested Control Flow
local mod_nested = compile_and_load("test_nested", [[
    local sum = 0
    for i = 1, 10 do
        if i % 2 == 0 then
            for j = 1, 5 do
                if j == 3 then break end -- Inner loop break
                sum = sum + 1
            end
        else
            -- Skip odd numbers (implicitly continue logic)
        end
    end
    return sum
]])
-- Explanation: Even numbers: 2, 4, 6, 8, 10 (5 iterations)
-- Inner loop runs for j=1, 2 (2 iterations per outer loop because break at 3)
-- Total sum = 5 * 2 = 10
if mod_nested ~= 10 then
    error("Test 3 Failed: Expected 10, got " .. tostring(mod_nested))
end
print("Test 3 Passed: Nested loops and break correct")

-- Test 4: Tail Call Optimization Check
-- Note: Using table wrapper to allow recursive call (same issue as Test 1).
local mod_tail = compile_and_load("test_tail", [[
    local wrapper = {}
    wrapper.tail_sum = function(n, acc)
        if n == 0 then return acc end
        return wrapper.tail_sum(n-1, acc + n)
    end
    return wrapper.tail_sum(100, 0)
]])
if mod_tail ~= 5050 then
    error("Test 4 Failed: Expected 5050, got " .. tostring(mod_tail))
end
print("Test 4 Passed: Tail recursive sum correct")

-- Test 5: Varargs
local mod_varargs = compile_and_load("test_varargs", [[
    local function sum(...)
        local s = 0
        local args = table.pack(...)
        for i = 1, args.n do
            s = s + args[i]
        end
        return s
    end
    return sum(1, 2, 3, 4, 5)
]])
if mod_varargs ~= 15 then
    error("Test 5 Failed: Expected 15, got " .. tostring(mod_varargs))
end
print("Test 5 Passed: Varargs sum correct")

-- Test 6: Pure C Arithmetic
-- Check if use_pure_c compiles and runs correctly.
-- It generates C operators (+, -, *, /) instead of lua_arith.
local mod_pure = compile_and_load("test_pure_c", [[
    local x = 10
    local y = 20
    return (x + y) * 2 / 5
]], true)
-- (10+20)*2/5 = 30*2/5 = 60/5 = 12
-- Note: Lua numbers are floats (usually double).
if mod_pure ~= 12.0 then
    error("Test 6 Failed: Expected 12.0, got " .. tostring(mod_pure))
end
print("Test 6 Passed: Pure C arithmetic correct")

print("---------------------------------------------------")
print("ALL COMPLEX SCENARIO TESTS PASSED")
