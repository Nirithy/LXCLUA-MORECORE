local ByteCode = require "ByteCode"

local function debug_print(...)
    -- print(...)
end

local function assert_equal(a, b, msg)
    if a ~= b then
        error("Assertion failed: " .. tostring(a) .. " != " .. tostring(b) .. " (" .. (msg or "") .. ")")
    end
end

print("Testing ByteCode improvements...")

-- 1. Test Constants
local function func_const()
    local s = "test_string"
    local n = 123.456
    return s, n
end

local p_const = ByteCode.GetProto(func_const)
local consts = ByteCode.GetConstants(p_const)

local found_s = false
local found_n = false
for _, v in ipairs(consts) do
    if v == "test_string" then found_s = true end
    if v == 123.456 then found_n = true end
end
assert(found_s, "String constant not found")
assert(found_n, "Number constant not found")

-- Test single constant access
-- We don't know the exact index, so we iterate to find index
local s_idx = -1
for i, v in ipairs(consts) do
    if v == "test_string" then s_idx = i break end
end
assert(s_idx ~= -1, "String constant index not found")
local s_val = ByteCode.GetConstant(p_const, s_idx)
assert_equal(s_val, "test_string", "GetConstant mismatch")


-- 2. Test Upvalues
local up_val = 10
local function func_upval()
    return up_val + 1
end

local p_upval = ByteCode.GetProto(func_upval)
local upvals = ByteCode.GetUpvalues(p_upval)
assert(#upvals == 1, "Expected 1 upvalue, got " .. #upvals)
assert_equal(upvals[1].name, "up_val", "Upvalue name mismatch")
-- Note: instack depends on whether it captures a local from immediate parent (1) or an upvalue from immediate parent (0).
-- Here `up_val` is local to the chunk, so for `func_upval` it is an upvalue.
-- `func_upval` is nested in the chunk. So `up_val` is captured from stack of parent.
-- So instack should be 1.
assert_equal(upvals[1].instack, 1, "Upvalue instack should be 1 (local in parent)")


local single_upval = ByteCode.GetUpvalue(p_upval, 1)
assert_equal(single_upval.name, "up_val", "GetUpvalue name mismatch")


-- 3. Test Locals
local function func_locals(a, b)
    local c = 1
    local d = 2
end

local p_locals = ByteCode.GetProto(func_locals)
local locals = ByteCode.GetLocals(p_locals)
-- Locals include parameters.
-- Lua 5.4: parameters are locals.
-- a, b, c, d.
-- Note: debug info might not be available if stripped, but we are running from source.
local found_a, found_b, found_c, found_d = false, false, false, false
for _, loc in ipairs(locals) do
    if loc.name == "a" then found_a = true end
    if loc.name == "b" then found_b = true end
    if loc.name == "c" then found_c = true end
    if loc.name == "d" then found_d = true end
end
assert(found_a, "Local 'a' not found")
assert(found_b, "Local 'b' not found")
assert(found_c, "Local 'c' not found")
assert(found_d, "Local 'd' not found")

local loc_1 = ByteCode.GetLocal(p_locals, 1)
assert(loc_1.name, "GetLocal failed")


-- 4. Test Nested Protos
local function func_nested()
    local function inner() return 1 end
    return inner
end

local p_nested = ByteCode.GetProto(func_nested)
local protos = ByteCode.GetNestedProtos(p_nested)
assert(#protos == 1, "Expected 1 nested proto")

local p_inner = ByteCode.GetNestedProto(p_nested, 1)
-- Verify it is a proto lightuserdata
assert(type(p_inner) == "userdata", "Nested proto should be userdata") -- lightuserdata is type 'userdata' in Lua
-- Can we get its code count?
local cnt = ByteCode.GetCodeCount(p_inner)
assert(cnt > 0, "Nested proto should have code")


-- 5. Test Instruction Manipulation (GetInstruction/SetInstruction)
local function func_inst()
    return 10
end
-- `return 10` compiles to:
-- LOADK (or LOADAsBx or LOADI)
-- RETURN

local p_inst = ByteCode.GetProto(func_inst)
local inst1 = ByteCode.GetInstruction(p_inst, 1)
assert(type(inst1) == "table", "GetInstruction should return table")
assert(inst1.OP, "Instruction table should have OP")
-- Check OpCode name
local op, opname = ByteCode.GetOpCode(inst1.OP)
debug_print("First instruction op: " .. opname)

-- Let's try to modify instruction.
-- We will change `return 10` to `return 20`.
-- This is hard because 20 might need to be in constants or fit in immediate.
-- Easier: Change `a = b + c` to `a = b - c`.
local function func_math(a, b)
    return a + b
end
-- ADD
local p_math = ByteCode.GetProto(func_math)
-- Find the ADD instruction.
local add_idx = -1
local cnt_math = ByteCode.GetCodeCount(p_math)
for i = 1, cnt_math do
    local inst = ByteCode.GetInstruction(p_math, i)
    local _, name = ByteCode.GetOpCode(inst.OP)
    if name == "ADD" then
        add_idx = i
        break
    end
end
assert(add_idx ~= -1, "ADD instruction not found")

-- Change ADD to SUB
local inst_add = ByteCode.GetInstruction(p_math, add_idx)
local _, sub_name = ByteCode.GetOpCode(ByteCode.OpCodes.SUB) -- Assuming OpCodes table exists or we find it
-- We need opcode value for SUB.
-- The library exposes `OpCodes` table in `luaopen_ByteCode`.
local OP_SUB = ByteCode.OpCodes.SUB
assert(OP_SUB, "OpCodes.SUB not found")

inst_add.OP = OP_SUB
ByteCode.SetInstruction(p_math, add_idx, inst_add)

-- Verify change
local res = func_math(10, 3)
assert_equal(res, 7, "Modified function should return 10 - 3 = 7, got " .. res)


print("ByteCode improvements tests passed!")
