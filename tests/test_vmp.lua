local vmprotect = require "vmprotect"

print("Testing vmprotect...")

local function safe_execute(f, ...)
  local status, res = pcall(f, ...)
  if not status then
    print("Error:", res)
    return nil
  end
  return res
end

-- Test 1: Simple Addition (OP_ADD)
local function test_add(a, b)
  return a + b
end

print("Protecting test_add...")
local p_add = vmprotect.protect(test_add)
print("p_add type:", type(p_add))

local res = safe_execute(p_add, 10, 20)
print("p_add(10, 20) =", res)
assert(res == 30, "Result should be 30")


-- Test 2: While Loop (OP_LE, OP_MUL, OP_ADD, OP_JMP)
local function factorial_while(n)
  local res = 1
  local i = 1
  while i <= n do
    res = res * i
    i = i + 1
  end
  return res
end

print("Protecting factorial_while...")
local p_fact = vmprotect.protect(factorial_while)
local res_fact = safe_execute(p_fact, 5)
print("p_fact(5) =", res_fact)
assert(res_fact == 120, "Result should be 120")


-- Test 3: Comparison (OP_EQ, OP_LT)
local function check_val(x)
  if x == 10 then
    return "equal"
  elseif x < 10 then
    return "less"
  else
    return "greater"
  end
end

print("Protecting check_val...")
local p_check = vmprotect.protect(check_val)
assert(safe_execute(p_check, 10) == "equal")
assert(safe_execute(p_check, 5) == "less")
assert(safe_execute(p_check, 15) == "greater")

print("All tests passed!")
