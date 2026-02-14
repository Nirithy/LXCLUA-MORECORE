local start_time = os.clock()

-- Test 1: Tight loop with conditional branch (OP_EQI / OP_JMP)
-- This triggers OP_EQI followed by OP_JMP.
-- If JIT exits on branch, this will be slow (interpreter speed).
local sum = 0
local limit = 100000000 -- 100M
for i = 1, limit do
  if i % 2 == 0 then
    sum = sum + 1
  else
    sum = sum + 2
  end
end
local end_time = os.clock()
print("Loop Time: " .. (end_time - start_time))
print("Sum: " .. sum)

-- Test 2: Recursive JIT call (OP_CALL)
-- To check for stack corruption or instability.
local function recursive(n)
  if n <= 0 then return 0 end
  return 1 + recursive(n - 1)
end

local start_rec = os.clock()
local res = recursive(1000)
local end_rec = os.clock()
print("Recursive Time: " .. (end_rec - start_rec))
print("Result: " .. res)

-- Test 3: Nested JIT calls
local function add(a, b)
  return a + b
end

local sum2 = 0
for i = 1, 1000000 do
  sum2 = add(sum2, 1)
end
print("Call Loop Sum: " .. sum2)
