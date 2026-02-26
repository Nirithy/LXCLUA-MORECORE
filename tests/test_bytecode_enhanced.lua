local ByteCode = require "ByteCode"

print("Testing ByteCode library enhancements...")

-- 1. Test OpCodes table
assert(type(ByteCode.OpCodes) == "table", "ByteCode.OpCodes missing")
assert(ByteCode.OpCodes.MOVE == 0, "OP_MOVE should be 0")
assert(ByteCode.OpCodes.RETURN ~= nil, "OP_RETURN should exist")

print("OpCodes table check passed.")

-- 2. Test GetOpCode
local function test_func()
    local a = 1
    return a
end

local p = ByteCode.GetProto(test_func)
local code_count = ByteCode.GetCodeCount(p)

for i = 1, code_count do
    local inst = ByteCode.GetCode(p, i)
    local op, name = ByteCode.GetOpCode(inst)
    assert(type(op) == "number", "OpCode should be number")
    assert(type(name) == "string", "OpName should be string")
    -- print(string.format("Inst %d: %s (%d)", i, name, op))
end

print("GetOpCode check passed.")

-- 3. Test Make and GetArgs
-- Create a MOVE instruction: R[0] = R[1]
-- MOVE is iABC, OpCode 0. A=0, B=1, C=0, k=0
local op_move = ByteCode.OpCodes.MOVE
local inst_move = ByteCode.Make(op_move, 0, 1, 0, 0)
local args = ByteCode.GetArgs(inst_move)

assert(args.OP == op_move, "OP mismatch")
assert(args.A == 0, "A mismatch")
assert(args.B == 1, "B mismatch")

-- Test Make with string opcode
local inst_move2 = ByteCode.Make("MOVE", 0, 1, 0, 0)
assert(inst_move == inst_move2, "Make with string opcode failed")

print("Make and GetArgs check passed.")

-- 4. Test Dump
print("Testing Dump function (output below):")
ByteCode.Dump(p)
print("Dump check passed.")

print("All ByteCode tests passed!")
