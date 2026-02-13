
local function test_add(a, b)
  return a + b
end

local function test_sub(a, b)
  return a - b
end

local function test_mixed(a, b, c)
  local d = a + b
  return d - c
end

print("Testing JIT Add/Sub...")

-- Run a few times to trigger JIT (if there was a counter, but currently it compiles on first run if supported)
-- However, ljit.c currently REJECTS anything starting with non-RETURN0.
-- So these won't be JITted yet.

local res_add = test_add(10, 20)
print("10 + 20 =", res_add)
assert(res_add == 30)

local res_sub = test_sub(30, 5)
print("30 - 5 =", res_sub)
assert(res_sub == 25)

local res_mixed = test_mixed(10, 20, 5)
print("(10 + 20) - 5 =", res_mixed)
assert(res_mixed == 25)

print("OK")
