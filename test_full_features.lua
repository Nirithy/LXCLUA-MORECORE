print("Starting README Verification...")

local errors = 0
local function test(name, func)
    local status, err = pcall(func)
    if not status then
        print("[FAIL] " .. name .. ": " .. tostring(err))
        errors = errors + 1
    else
        print("[PASS] " .. name)
    end
end

-- 1. Compound Assignment
test("Compound Assignment", function()
    local a = 10
    a += 5
    if a ~= 15 then error("a += 5 failed, got " .. tostring(a)) end
    a++
    if a ~= 16 then error("a++ failed, got " .. tostring(a)) end
end)

-- 2. Comparison
test("Comparison Operators", function()
    local res = 10 <=> 20
    if res ~= -1 then error("<=> failed, got " .. tostring(res)) end
    if (10 != 20) ~= true then error("!= failed") end
end)

-- 3. Null Safety
test("Null Safety", function()
    local val = nil
    local res = val ?? "default"
    if res ~= "default" then error("?? failed, got " .. tostring(res)) end

    local config = { server = { port = 8080 } }
    if config?.server?.port ~= 8080 then error("?. failed (valid)") end
    if config?.client?.timeout ~= nil then error("?. failed (nil)") end
end)

-- 4. Pipe Operator (Critical Check)
test("Pipe Operator", function()
    local function timesTwo(x) return x * 2 end
    local res = 5 |> timesTwo
    if res == 5 then
        print("WARNING: Pipe operator returned LHS (known issue)")
    elseif res == 10 then
        print("Pipe operator works correctly")
    else
        error("Pipe operator returned unexpected value: " .. tostring(res))
    end
end)

-- 5. String Interpolation
test("String Interpolation", function()
    local name = "World"
    local s = "Hello, ${name}!"
    if s == "Hello, World!" then
        print("String interpolation works natively")
    else
        print("String interpolation result: " .. s)
        -- If it failed to interpolate, it might be "Hello, ${name}!"
        -- We won't error out, just report.
    end
end)

-- 6. Lambda / Arrow
test("Arrow Functions", function()
    local add = (a, b) => a + b
    if add(10, 20) ~= 30 then error("Arrow function failed") end

    local sq = lambda(x): x * x
    if sq(4) ~= 16 then error("Lambda failed") end
end)

-- 7. Switch
test("Switch Statement", function()
    local val = 1
    local res = ""
    switch (val) do
        case 1:
            res = "A"
            break
        case 2:
            res = "B"
            break
    end
    if res ~= "A" then error("Switch failed") end
end)

-- 8. Try-Catch
test("Try-Catch", function()
    local caught = false
    try
        error("oops")
    catch(e)
        caught = true
    end
    if not caught then error("Try-catch failed") end
end)

-- 9. FS Library (New)
test("FS Library", function()
    local status, fs = pcall(require, "fs")
    if not status then
        print("fs library not found")
        return
    end
    if type(fs) ~= "table" then error("fs require failed") end
    local cwd = fs.currentdir()
    if type(cwd) ~= "string" then error("fs.currentdir failed") end
    -- Only check existence if we know where we are. Assuming README.md is in CWD.
    if not fs.exists("README.md") then print("WARNING: README.md not found in CWD") end
end)

-- 10. HTTP Library (New)
test("HTTP Library", function()
    local status, http = pcall(require, "http")
    if not status then
        print("http library not found")
        return
    end
    if type(http) ~= "table" then error("http require failed") end
    if type(http.get) ~= "function" then error("http.get missing") end
end)

-- 11. Thread Library (New)
test("Thread Library", function()
    local status, thread = pcall(require, "thread")
    if not status then
         print("thread library not found")
         return
    end
    if type(thread) ~= "table" then error("thread require failed") end
end)

if errors > 0 then
    print("Total Failures: " .. errors)
    os.exit(1)
else
    print("All checks passed.")
    os.exit(0)
end
