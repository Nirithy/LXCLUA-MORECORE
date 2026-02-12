-- test_table_ops.lua

print("Checking table.insert:", table.insert)

print("Testing __concat (..)")
local t1 = {1, 2}
local t2 = {3, 4}
local t3 = t1 .. t2
assert(#t3 == 4)
assert(t3[1] == 1)
assert(t3[2] == 2)
assert(t3[3] == 3)
assert(t3[4] == 4)
print("  Success: {1, 2} .. {3, 4} -> {1, 2, 3, 4}")

print("Testing __add (+)")
local t4 = {1, 2, 3}
local t5 = {3, 4, 5}
local t6 = t4 + t5
-- Result should be {1, 2, 3, 4, 5} (unique)
assert(#t6 == 5)
local seen = {}
for _, v in ipairs(t6) do seen[v] = true end
assert(seen[1] and seen[2] and seen[3] and seen[4] and seen[5])
print("  Success: {1, 2, 3} + {3, 4, 5} -> {1, 2, 3, 4, 5} (unique)")

print("Testing __sub (-)")
local t7 = {1, 2, 3, 4}
local t8 = {2, 4}
local t9 = t7 - t8
-- Result should be {1, 3}
assert(#t9 == 2)
assert(t9[1] == 1)
assert(t9[2] == 3)
print("  Success: {1, 2, 3, 4} - {2, 4} -> {1, 3}")

print("Testing OO syntax")
local t10 = {10, 20}

-- Call table.insert directly first
table.insert(t10, 25)
assert(t10[3] == 25)
print("  Success: table.insert(t10, 25)")

t10:insert(30) -- Should call table.insert(t10, 30)
assert(#t10 == 4)
assert(t10[4] == 30)
print("  Success: t:insert(30)")

t10:remove(2) -- Remove 20. t10 becomes {10, 25, 30}
assert(#t10 == 3)
assert(t10[2] == 25)
print("  Success: t:remove(2)")

local s = t10:concat(",")
-- 10,25,30
assert(s == "10,25,30")
print("  Success: t:concat(',')")

print("Testing Custom Metatable Override/Inheritance")
local t11 = {}
local mt = {
    __index = { foo = function() return "bar" end },
    __add = function(a, b) return "custom_add" end
}
setmetatable(t11, mt)

print("t11.foo is:", t11.foo)
if t11.foo then
    print("t11.foo() result:", t11.foo(t11))
end

assert(t11.foo(t11) == "bar")

-- Check that default behavior is preserved via chain!
-- Since mt.__index is a table without its own MT, it inherits DefaultMT.
-- So t11:insert(1) SHOULD work.
local status, err = pcall(function() return t11:insert(1) end)
assert(status == true)
assert(t11[1] == 1)
print("  Success: Custom metatable falls back to default behavior (inheritance)")

local res = t11 + t11
assert(res == "custom_add")
print("  Success: Custom metatable __add works (overrides default)")

print("All table ops tests passed!")
