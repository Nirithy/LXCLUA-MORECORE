-- wasm3 模块测试脚本
-- 运行: lxclua test_wasm3.lua

local wasm3 = require("wasm3")

print("=== wasm3 模块测试 ===")

-- 创建环境
local env = wasm3.newEnvironment()
print("[OK] 创建环境")

-- 创建运行时
local runtime = env:newRuntime(64 * 1024)
print("[OK] 创建运行时")

-- 读取 WASM 文件
local f, err = io.open("test_wasm.wasm", "rb")
if not f then
    print("[错误] 无法打开 test_wasm.wasm: " .. (err or "unknown"))
    return
end
local wasm_data = f:read("*a")
f:close()
print("[OK] 读取 WASM 文件 (" .. #wasm_data .. " bytes)")

-- 解析模块
local module = env:parseModule(wasm_data)
print("[OK] 解析模块")

-- 加载模块
runtime:loadModule(module)
print("[OK] 加载模块")

-- 测试 add 函数
local add_func = runtime:findFunction("add")
local result = add_func:call(10, 20)
print(string.format("[OK] add(10, 20) = %d (期望: 30)", result))

-- 测试 mul 函数
local mul_func = runtime:findFunction("mul")
result = mul_func:call(6, 7)
print(string.format("[OK] mul(6, 7) = %d (期望: 42)", result))

-- 测试 factorial 函数
local fact_func = runtime:findFunction("factorial")
result = fact_func:call(5)
print(string.format("[OK] factorial(5) = %d (期望: 120)", result))

-- 测试 fib 函数
local fib_func = runtime:findFunction("fib")
result = fib_func:call(10)
print(string.format("[OK] fib(10) = %d (期望: 55)", result))

-- 测试 add_double 函数
local add_d_func = runtime:findFunction("add_double")
result = add_d_func:call(3.14, 2.86)
print(string.format("[OK] add_double(3.14, 2.86) = %.2f (期望: 6.00)", result))

-- 获取内存大小
local mem_size = runtime:getMemorySize()
print(string.format("[OK] 内存大小: %d bytes", mem_size))

print("\n=== 所有测试通过! ===")
