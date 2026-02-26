local ByteCode = require "ByteCode"

local function assert_true(v, msg)
  if not v then error(msg or "assertion failed: expected true", 2) end
end

local function assert_false(v, msg)
  if v then error(msg or "assertion failed: expected false", 2) end
end

local function assert_error(func, msg_pattern)
  local status, err = pcall(func)
  if status then
    error("expected error but got success", 2)
  end
  if msg_pattern and not string.find(tostring(err), msg_pattern) then
    error(string.format("error message '%s' did not match pattern '%s'", tostring(err), msg_pattern), 2)
  end
end

print("Testing ByteCode Security Features...")

-- 1. Test Lock
print("1. Testing Lock...")
local function f1() return 1 end
local p1 = ByteCode.GetProto(f1)

assert_false(ByteCode.IsLocked(p1), "Initial state should be unlocked")

-- Get instructions before locking
local inst = ByteCode.GetCode(p1, 1)
local inst_tbl = ByteCode.GetInstruction(p1, 1)

ByteCode.Lock(p1)
assert_true(ByteCode.IsLocked(p1), "Should be locked after Lock()")

-- Test debug.getinfo
local info = debug.getinfo(f1, "k")
assert_true(info.islocked, "debug.getinfo should report islocked=true")

-- Test GetCode (should now fail)
assert_error(function()
  ByteCode.GetCode(p1, 1)
end, "function is locked")

-- Test SetCode
assert_error(function()
  ByteCode.SetCode(p1, 1, inst)
end, "function is locked")

-- Test GetInstruction (should now fail)
assert_error(function()
  ByteCode.GetInstruction(p1, 1)
end, "function is locked")

-- Test SetInstruction
assert_error(function()
  ByteCode.SetInstruction(p1, 1, inst_tbl)
end, "function is locked")

print("   Lock test passed.")

-- 2. Test MarkOriginal and IsTampered
print("2. Testing Tamper Detection...")
local function f2() return 2 end
local p2 = ByteCode.GetProto(f2)

-- Initially not marked, IsTampered should be false
assert_false(ByteCode.IsTampered(p2), "Should not be tampered initially")

-- Mark Original
ByteCode.MarkOriginal(p2)
assert_false(ByteCode.IsTampered(p2), "Should not be tampered after MarkOriginal")

-- debug.getinfo check
local info2 = debug.getinfo(f2, "T")
assert_false(info2.istampered, "debug.getinfo should report istampered=false")

-- Modify code (before locking)
local inst2 = ByteCode.GetCode(p2, 1)
-- Make a no-op change or same instruction but we need to verify tampering.
-- Let's change it to RETURN 0 (OP_RETURN A=0 B=1) -> 26 0 1
-- Or just change the existing instruction slightly if possible, or same instruction?
-- If I write the same instruction, hash should be same.
ByteCode.SetCode(p2, 1, inst2)
assert_false(ByteCode.IsTampered(p2), "Writing same instruction should not trigger tamper (same hash)")

-- Change instruction effectively
-- Create a RETURN instruction
local new_inst = ByteCode.Make("RETURN", 0, 1)
-- Ensure it's different
if new_inst == inst2 then
    new_inst = ByteCode.Make("RETURN", 1, 1)
end

ByteCode.SetCode(p2, 1, new_inst)
assert_true(ByteCode.IsTampered(p2), "Should be tampered after modification")

-- debug.getinfo check
info2 = debug.getinfo(f2, "T")
assert_true(info2.istampered, "debug.getinfo should report istampered=true")

-- Re-mark original
ByteCode.MarkOriginal(p2)
assert_false(ByteCode.IsTampered(p2), "Should be reset after new MarkOriginal")

print("   Tamper detection passed.")

-- 3. Test Hotfix Prevention
print("3. Testing Hotfix Prevention...")
local up = 0
local function f3() return up end
local function f3_new() return up + 1 end

local p3 = ByteCode.GetProto(f3)
ByteCode.Lock(p3)

-- Check that hotfix is blocked
if debug.hotfix then
  assert_error(function()
    debug.hotfix(f3, f3_new)
  end, "attempt to hotfix a locked function")
else
  print("   (debug.hotfix not available, skipping)")
end

print("   Hotfix prevention passed.")

print("All ByteCode Security Tests Passed!")
