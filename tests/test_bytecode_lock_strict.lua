local ByteCode = require "ByteCode"

local function assert_error(func, msg_pattern)
  local status, err = pcall(func)
  if status then
    error("expected error but got success", 2)
  end
  if msg_pattern and not string.find(tostring(err), msg_pattern) then
    error(string.format("error message '%s' did not match pattern '%s'", tostring(err), msg_pattern), 2)
  end
end

print("Testing ByteCode Strict Locking...")

local function foo() return 1 end
local p = ByteCode.GetProto(foo)

-- Initially not locked
print("Locking function...")
ByteCode.Lock(p)
assert(ByteCode.IsLocked(p), "Function should be locked")

-- Test SetCode
print("Testing SetCode on locked function...")
assert_error(function()
  local inst = ByteCode.GetCode(p, 1)
  ByteCode.SetCode(p, 1, inst)
end, "function is locked")

-- Test SetInstruction
print("Testing SetInstruction on locked function...")
assert_error(function()
  local inst = ByteCode.GetInstruction(p, 1)
  ByteCode.SetInstruction(p, 1, inst)
end, "function is locked")

-- Test MarkOriginal
print("Testing MarkOriginal on locked function...")
assert_error(function()
  ByteCode.MarkOriginal(p)
end, "function is locked")

-- Test IsGC
print("Testing IsGC on locked function...")
assert_error(function()
  ByteCode.IsGC(p)
end, "function is locked")

print("All Strict Locking Tests Passed!")
