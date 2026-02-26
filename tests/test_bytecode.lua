local ByteCode = require "ByteCode"

print("Testing ByteCode module...")

-- Test CheckFunction
local function test_func(a, b)
  return a + b
end

assert(ByteCode.CheckFunction(test_func) == true, "CheckFunction failed for Lua function")
assert(ByteCode.CheckFunction(print) == false, "CheckFunction failed for C function")
assert(ByteCode.CheckFunction({}) == false, "CheckFunction failed for table")

print("CheckFunction passed")

-- Test GetProto
local proto = ByteCode.GetProto(test_func)
assert(proto, "GetProto returned nil")
print("GetProto passed")

-- Test IsGC (Pinning)
ByteCode.IsGC(proto)
print("IsGC passed")

-- Test GetCodeCount
local count = ByteCode.GetCodeCount(proto)
print("Code count:", count)
assert(count > 0, "GetCodeCount returned 0")

-- Test GetParamCount
local params = ByteCode.GetParamCount(proto)
print("Param count:", params)
assert(params == 2, "GetParamCount returned wrong value")

-- Test GetLine
for i = 1, count do
  local line = ByteCode.GetLine(proto, i)
  -- line can be 0 or correct line
  -- print("Line for instruction " .. i .. ":", line)
end
print("GetLine passed")

-- Test GetCode and SetCode
-- We will read an instruction and write it back
local inst = ByteCode.GetCode(proto, 1)
print(string.format("Instruction 1: 0x%X", inst))

ByteCode.SetCode(proto, 1, inst)
local inst2 = ByteCode.GetCode(proto, 1)
assert(inst == inst2, "SetCode verification failed")

print("GetCode/SetCode passed")

print("All ByteCode tests passed!")
