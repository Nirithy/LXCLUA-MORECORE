print("=== Starting JIT Evaluation ===")

local function run_test(name, func)
    print("\n[TEST] Running " .. name)
    local status, err = pcall(func)
    if status then
        print("[TEST] " .. name .. ": SUCCESS")
    else
        print("[TEST] " .. name .. ": FAILED - " .. tostring(err))
    end
end

-- 1. Arithmetic
run_test("test_math", function()
    local a = 10
    local b = 20
    local c = a + b
    local d = c * 2
    local e = d / 5
    local f = e - 2
    if f ~= 10.0 and f ~= 10 then error("Math result incorrect: " .. tostring(f)) end
end)

-- 2. Logic
run_test("test_logic", function()
    local a = 10
    local b = 20
    if a > b then error("a > b") end
    if b < a then error("b < a") end
    if a == b then error("a == b") end
    if not (a ~= b) then error("not (a ~= b)") end
end)

-- 3. Tables
run_test("test_tables", function()
    local t = {}
    t.x = 10
    t.y = 20
    t["z"] = 30
    if t.x + t.y ~= 30 then error("Table access failed") end
    if t.z ~= 30 then error("Table string key failed") end
end)

-- 4. Calls
local function add(a, b)
    return a + b
end

run_test("test_calls", function()
    local res = add(10, 20)
    if res ~= 30 then error("Call result incorrect") end
end)

-- 5. Recursion
local function fib(n)
    if n < 2 then return n end
    return fib(n-1) + fib(n-2)
end

run_test("test_recursion", function()
    local res = fib(10)
    if res ~= 55 then error("Recursion result incorrect: " .. res) end
end)

-- 6. Loops
run_test("test_loops", function()
    local sum = 0
    for i = 1, 10 do
        sum = sum + i
    end
    if sum ~= 55 then error("For loop sum incorrect: " .. sum) end

    local j = 0
    while j < 5 do
        j = j + 1
    end
    if j ~= 5 then error("While loop result incorrect") end
end)

-- 7. Upvalues
local function make_counter()
    local count = 0
    return function()
        count = count + 1
        return count
    end
end

run_test("test_upvalues", function()
    local c = make_counter()
    if c() ~= 1 then error("Upvalue 1 incorrect") end
    if c() ~= 2 then error("Upvalue 2 incorrect") end
end)

print("\n=== JIT Evaluation Complete ===")
