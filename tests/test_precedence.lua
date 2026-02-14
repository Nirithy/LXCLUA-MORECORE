local function assert_eq(a, b, msg)
    if a ~= b then
        error("Assertion failed: " .. tostring(a) .. " ("..type(a)..")" .. " ~= " .. tostring(b) .. " ("..type(b)..")" .. (msg and " (" .. msg .. ")" or ""))
    end
end

-- Test 1: 1/3[7] (The fix)
local res1 = 1/3[7]
assert_eq(res1, "0.3333333", "1/3[7] failed")

-- Test 2: 3[7] (Direct indexing)
local res2 = 3[7]
assert_eq(res2, "3.0000000", "3[7] failed")

-- Test 3: (1/3)[7] (Explicit parens)
local res3 = (1/3)[7]
assert_eq(res3, "0.3333333", "(1/3)[7] failed")

-- Test 4: a[1] (Standard table indexing)
local t = {10, 20}
local a = t[1]
assert_eq(a, 10, "t[1] failed")

-- Test 5: a[1] + 2 (Precedence check: should be (a[1]) + 2)
local res5 = t[1] + 2
assert_eq(res5, 12, "t[1] + 2 failed")

-- Test 6: 1 + 2 * 3 (Standard precedence check)
local res6 = 1 + 2 * 3
assert_eq(res6, 7, "1 + 2 * 3 failed")

-- Test 7: x .. y[2] (Concat vs Index)
-- Expected: x .. (y[2]) because [ (10) > .. (9)
local x = "Val: "
local y = 5
local res7 = x .. y[2]
assert_eq(res7, "Val: 5.00", "x .. y[2] failed")

-- Test 8: 1 + 2[2] (Add vs Index)
-- Expected: (1 + 2)[2] because [ (10) < + (11)
-- 3[2] -> "3.00"
local res8 = 1 + 2[2]
assert_eq(res8, "3.00", "1 + 2[2] failed")

-- Test 9: Complex arithmetic
-- (10 + 5)[2] -> "15.00"
-- 10 + 5[2] -> (10+5)[2] -> "15.00"
local res9a = (10 + 5)[2]
assert_eq(res9a, "15.00", "(10 + 5)[2] failed")

local res9b = 10 + 5[2]
assert_eq(res9b, "15.00", "10 + 5[2] failed")

print("All precedence tests passed!")
