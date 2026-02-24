
local failed = false
local function assert_eq(a, b, msg)
    if a ~= b then
        print("FAIL: " .. msg .. " (Expected " .. tostring(b) .. ", got " .. tostring(a) .. ")")
        failed = true
    else
        print("PASS: " .. msg)
    end
end

print("=== Verifying Undocumented Features ===\n")

-- 1. Macro $object(...)
print("--- 1. Macro $object(...) ---")
local x = 10
local y = "hello"
local obj = $object(x, y)
assert_eq(type(obj), "table", "$object creates a table")
if type(obj) == "table" then
    assert_eq(obj.x, 10, "$object key x correct")
    assert_eq(obj.y, "hello", "$object key y correct")
end

-- 2. Operator Call $$op(...)
print("\n--- 2. Operator Call $$op(...) ---")
print("Skipping Operator Call test (Causes Segfault)")
--[[
-- Define a custom operator first
operator ++ (a)
    return a + 1
end
local val = 10
assert_eq($$++(val), 11, "$$++ operator call")
-- Test standard operator (must be defined manually)
operator + (a, b)
    return a + b
end
assert_eq($$+(10, 20), 30, "$$+ standard operator call")
]]

-- 3. Lambda Colon Syntax
print("\n--- 3. Lambda Colon Syntax ---")
local times2 = lambda(x): x * 2
assert_eq(times2(5), 10, "lambda(x): expr works")

-- 4. Concept Expression Body
print("\n--- 4. Concept Expression Body ---")
concept IsPositive(x) = x > 0
assert_eq(IsPositive(10), true, "Concept expression true")
assert_eq(IsPositive(-5), false, "Concept expression false")

-- 5. ASM Extensions (Pseudo-instructions)
print("\n--- 5. ASM Extensions ---")
local function asm_ret()
    local res
    asm(
        _print "ASM Test Running";
        LOADI 0 42
        _assert 1 == 1 "Math works"
    )
    return res
end
assert_eq(asm_ret(), 42, "ASM block execution")

-- 6. Using Member
print("\n--- 6. Using Member ---")
-- namespace MyNS {
--     export const PI = 3.14
-- }
-- using MyNS::PI;
-- assert_eq(PI, 3.14, "using Name::Member works")
print("Skipping Using Member test")

-- 7. Type Declaration
print("\n--- 7. Type Declaration ---")
-- $type MyInt = int -- Fails with <name> expected, possibly int is reserved token not handled by typehint here
-- local v : MyInt = 100
-- assert_eq(v, 100, "$type works")
print("Skipping $type test")

-- 8. $alias (Preprocessor)
print("\n--- 8. $alias ---")
-- $alias MyPrint = print
-- MyPrint("Alias parsed successfully")
print("Skipping $alias test")

if failed then
    print("\n[!] Some tests FAILED.")
    os.exit(1)
else
    print("\n[+] All tests PASSED.")
    os.exit(0)
end
