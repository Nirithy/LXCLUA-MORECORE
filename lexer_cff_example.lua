-- lexer_cff_example.lua
-- This example demonstrates how to use the lexer library to perform a simple
-- Control-Flow Flattening (CFF) obfuscation. We take a function with sequential
-- statements, split them into blocks using lexer utilities, and rebuild the function
-- using a switch-based state machine.

local cff = require("cff")

local code = [[
local function example()
  local a = 10
  local b = 20
  if a > 5 then
    a = a + 5
  else
    b = b + 5
  end
  print("Result: ", a + b)
end
example()
]]

print("--- Original Code ---")
print(code)

print("--- CFF Obfuscated Code ---")
local refactored = cff.obfuscate(code)
print(refactored)

print("--- Execution Output ---")
local f, err = load(refactored)
if f then
    f()
else
    print("Failed to load refactored code:", err)
end
