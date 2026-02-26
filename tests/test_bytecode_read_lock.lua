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

print("Testing ByteCode Read Locking...")

local function test() return "hello" end

-- 1. Test Unlocked Access
local p = ByteCode.GetProto(test)
print("Testing unlocked function...")

local count = ByteCode.GetCodeCount(p)
assert(count > 0, "GetCodeCount should work on unlocked function")

-- Test Dump (redirect stdout to avoid clutter)
-- We can't easily redirect stdout in pure Lua without os.execute or similar, but Dump prints to stdout.
-- Just call it and assume it works if no error.
ByteCode.Dump(p)

-- 2. Lock Function
print("Locking function...")
ByteCode.Lock(p)
assert(ByteCode.IsLocked(p), "Function should be locked")

-- 3. Test Locked Access (Read Operations)
print("Testing locked function access...")

assert_error(function() ByteCode.GetCodeCount(p) end, "function is locked")
assert_error(function() ByteCode.GetCode(p, 1) end, "function is locked")
assert_error(function() ByteCode.GetLine(p, 1) end, "function is locked")
assert_error(function() ByteCode.GetParamCount(p) end, "function is locked")
assert_error(function() ByteCode.Dump(p) end, "function is locked")
assert_error(function() ByteCode.GetConstant(p, 1) end, "function is locked")
assert_error(function() ByteCode.GetConstants(p) end, "function is locked")
assert_error(function() ByteCode.GetUpvalue(p, 1) end, "function is locked")
assert_error(function() ByteCode.GetUpvalues(p) end, "function is locked")
assert_error(function() ByteCode.GetLocal(p, 1) end, "function is locked")
assert_error(function() ByteCode.GetLocals(p) end, "function is locked")
-- Nested protos might not exist in this simple function, but if they did:
-- assert_error(function() ByteCode.GetNestedProto(p, 1) end, "function is locked")
-- assert_error(function() ByteCode.GetNestedProtos(p) end, "function is locked")
assert_error(function() ByteCode.GetInstruction(p, 1) end, "function is locked")

-- 4. Test Allowed Operations
-- IsLocked and IsTampered should still work?
-- IsTampered (if implemented to check hash) might rely on reading bytecode?
-- If IsTampered just compares hash stored in Proto vs calculated hash, calculating hash requires reading bytecode.
-- My change blocked access. But does IsTampered use internal access or ByteCode library functions?
-- It uses internal access in C. I did not add checks to `bytecode_istampered`.
-- So it should work.

assert(ByteCode.IsLocked(p) == true, "IsLocked should return true")
-- MarkOriginal is blocked by lock check (existing behavior), but IsTampered is read-only.
-- Since I didn't add lock check to IsTampered, it should work.
local tampered = ByteCode.IsTampered(p)
assert(tampered == false, "IsTampered should return false (not tampered)")

print("All Read Locking Tests Passed!")
