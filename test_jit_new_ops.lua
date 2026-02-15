local function test_not(a)
    return not a
end

local function test_bitwise(a, b)
    local x = a & b
    local y = a | b
    local z = a ~ b
    return x, y, z
end

local function test_table(t, k, v)
    t[k] = v
    return t[k]
end

print("Testing JIT New Opcodes...")

-- Trigger JIT for test_not
for i = 1, 1000 do
    assert(test_not(false) == true)
    assert(test_not(true) == false)
    assert(test_not(nil) == true)
    assert(test_not(1) == false)
end
print("OP_NOT passed")

-- Trigger JIT for bitwise
for i = 1, 1000 do
    local x, y, z = test_bitwise(0xF0, 0x0F)
    assert(x == 0x00)
    assert(y == 0xFF)
    assert(z == 0xFF)
end
print("Bitwise passed")

-- Trigger JIT for table operations
local t = {}
for i = 1, 1000 do
    assert(test_table(t, "key", i) == i)
    assert(test_table(t, 1, i) == i)
end
print("Table Ops passed")

print("All tests passed!")
