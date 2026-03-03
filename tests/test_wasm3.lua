-- Simple WASM binary:
-- (module
--   (func (export "add") (param i32 i32) (result i32)
--     local.get 0
--     local.get 1
--     i32.add)
-- )
local wasm_bytes = string.char(
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01,
    0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, 0x0a, 0x09,
    0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b
)

local wasm3 = require("wasm3")

print("wasm3 library loaded successfully")

local env = wasm3.newEnvironment()
print("WASM3 environment created:", env)

local module = env:parseModule(wasm_bytes)
print("WASM module parsed:", module)

local runtime = env:newRuntime(1024)
print("WASM runtime created:", runtime)

-- optional: check name
module:setName("test_module")
print("Module name set to:", module:getName())

runtime:loadModule(module)
print("WASM module loaded into runtime")

local memorySize = runtime:getMemorySize()
print("Runtime memory size:", memorySize)

local add_func = runtime:findFunction("add")
print("Found function 'add':", add_func)

local result = add_func:call(10, 20)
print("Result of add(10, 20) =", result)

assert(result == 30, "add(10, 20) should return 30")

print("WASM3 test passed!")
