-- Test file for mixed Arrow Function and Case Operator syntax

print("Testing Arrow Function Syntax support...")

local errors = 0
local function assert_eq(a, b, msg)
    if a ~= b then
        print("FAIL: " .. (msg or "") .. " Expected " .. tostring(b) .. ", got " .. tostring(a))
        errors = errors + 1
    else
        print("PASS: " .. (msg or ""))
    end
end

local function assert_type(obj, type_name, msg)
    if type(obj) ~= type_name then
        print("FAIL: " .. (msg or "") .. " Expected type " .. type_name .. ", got " .. type(obj))
        errors = errors + 1
    else
        print("PASS: " .. (msg or "") .. " is " .. type_name)
    end
end

-- 1. Infix Case Operator (a => b) ---
print("\n--- 1. Infix Case Operator (a => b) ---")
local a = 10
local b = 20
local case_obj = a => b
assert_type(case_obj, "table", "a => b should return a table")
if type(case_obj) == "table" then
    assert_eq(case_obj[1], 10, "Case key")
    assert_eq(case_obj[2], 20, "Case value")
end

-- 2. Prefix Arrow Function (Existing syntax)
print("\n--- 2. Prefix Arrow Function (=> ...) ---")
local f1 = =>(x) { x * 2 }
assert_type(f1, "function", "=> ... should return a function")
assert_eq(f1(5), 10, "f1(5)")

-- 3. Standard Arrow Function Syntax (New support)
print("\n--- 3. Standard Arrow Syntax ((args) => ...) ---")

-- 3a. Single arg with parens: (x) => expr
local f2 = (x) => x * 3
assert_type(f2, "function", "(x) => expr should return a function")
assert_eq(f2(5), 15, "f2(5)")

-- 3c. Multiple args: (x, y) => expr
local f4 = (x, y) => x + y
assert_type(f4, "function", "(x, y) => expr should return a function")
assert_eq(f4(10, 20), 30, "f4(10, 20)")

-- 3d. No args: () => expr
local f5 = () => 42
assert_type(f5, "function", "() => expr should return a function")
assert_eq(f5(), 42, "f5()")

-- 3e. Varargs: (...) => expr
local f6 = (...) => table.concat({...}, ",")
assert_type(f6, "function", "(...) => expr should return a function")
assert_eq(f6("a", "b", "c"), "a,b,c", "f6(...)")

-- 3f. Mixed args: (a, ...) => expr
local f7 = (a, ...) => a .. "-" .. table.concat({...}, "")
assert_type(f7, "function", "(a, ...) => expr should return a function")
assert_eq(f7("head", "tail"), "head-tail", "f7(...)")

-- 4. Ambiguity/Conflict Check
print("\n--- 4. Ambiguity Checks ---")

-- (a) => b  vs  a => b
local val = 100
local res_case = val => val + 1
local res_func = (val) => val + 1

assert_type(res_case, "table", "val => ... (no parens) is Case Operator")
assert_type(res_func, "function", "(val) => ... (parens) is Arrow Function")

-- Pick test simulation
local function mock_pick(cases)
    print("Mock pick received " .. #cases .. " cases")
    for i, c in ipairs(cases) do
        if type(c) == "table" then
            print("  Case " .. i .. ": " .. type(c[1]) .. " => " .. type(c[2]))
        elseif type(c) == "function" then
            print("  Case " .. i .. ": Arrow Function (Single Param)")
        else
            print("  Case " .. i .. ": Unknown type " .. type(c))
        end
    end
    -- Check expected types
    assert_type(cases[1], "table", "Case 1 is Case Operator")
    assert_type(cases[2], "function", "Case 2 is Arrow Function")
end

print("Simulating pick usage:")
mock_pick {
    "event" => function() return "handled" end,
    (val) => val  -- This creates a function in the list
}

print("\nSummary:")
if errors == 0 then
    print("All tests PASSED.")
else
    print(errors .. " tests FAILED.")
    os.exit(1)
end
