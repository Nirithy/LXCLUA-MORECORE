local clock = os.clock

print("--- JIT Test Start ---")

-- Test 1: Integer Arithmetic
local function test_arith()
    local sum = 0
    for i = 1, 10000 do
        sum = sum + i
    end
    return sum
end

print("Running Arithmetic Test...")
local t1 = clock()
for i = 1, 500 do
    test_arith()
end
print("Arithmetic Test Done. Time: " .. (clock() - t1))

print("--- JIT Test End ---")
