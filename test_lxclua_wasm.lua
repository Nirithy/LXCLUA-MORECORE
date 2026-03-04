-- 测试 lxclua.wasm 模块
-- 运行: lxclua test_lxclua_wasm.lua

local wasm3 = require("wasm3")

print("=== lxclua.wasm 模块测试 ===\n")

-- 创建环境
local env = wasm3.newEnvironment()
local runtime = env:newRuntime(4 * 1024 * 1024)  -- 4MB 栈

-- 读取 WASM 文件
local f = io.open("lxclua.wasm", "rb")
if not f then
    print("[错误] 请先编译: make lxclua-wasm")
    return
end
local wasm_data = f:read("*a")
f:close()

print(string.format("[OK] 读取 lxclua.wasm (%d bytes)", #wasm_data))

-- 解析模块
local module = env:parseModule(wasm_data)
print("[OK] 解析模块")

-- 加载模块
runtime:loadModule(module)
print("[OK] 加载模块")

-- 链接必要的库（必须在 loadModule 之后）
module:linkLibC()
print("[OK] 链接 LibC")

-- 辅助函数
local function call(name, ...)
    local func = runtime:findFunction(name)
    return func:call(...)
end

-- ==================== 测试 Lua State ====================
print("\n--- 测试 Lua State ---")

-- 创建 Lua state
local L = call("lua_wasm_newstate")
print(string.format("[OK] 创建 Lua State: 0x%X", L))

-- 打开标准库
call("lua_wasm_openlibs", L)
print("[OK] 打开标准库")

-- 获取版本
local version = call("lua_wasm_version")
print(string.format("[OK] Lua 版本: %.2f", version))

-- ==================== 测试代码执行 ====================
print("\n--- 测试代码执行 ---")

-- 执行简单代码
local result = call("lua_wasm_dostring", L, "return 1 + 2")
print(string.format("[OK] 执行 'return 1 + 2': 结果类型=%d", result))

-- 获取栈顶
local top = call("lua_wasm_gettop", L)
print(string.format("[OK] 栈顶索引: %d", top))

-- 获取结果
local num = call("lua_wasm_tonumber", L, -1)
print(string.format("[OK] 获取结果: 1 + 2 = %.0f", num))

-- 弹出结果
call("lua_wasm_pop", L, 1)

-- ==================== 测试高级 API ====================
print("\n--- 测试高级 API ---")

-- 使用 eval 执行代码
local eval_result = call("lua_wasm_eval", L, "return 'Hello from WASM!'")
print(string.format("[OK] eval 字符串结果: %s", eval_result))

-- 使用 eval_number
call("lua_wasm_dostring", L, "function fib(n) if n <= 1 then return n end return fib(n-1) + fib(n-2) end")
call("lua_wasm_dostring", L, "function test() return fib(10) end")

local fib_result = call("lua_wasm_call_global_number", L, "test")
print(string.format("[OK] fib(10) = %.0f", fib_result))

-- ==================== 测试全局变量 ====================
print("\n--- 测试全局变量 ---")

-- 设置全局变量
call("lua_wasm_setglobal_number", L, "my_number", 42.5)
call("lua_wasm_setglobal_integer", L, "my_integer", 100)
call("lua_wasm_setglobal_string", L, "my_string", "Hello WASM!")

-- 获取全局变量
local my_num = call("lua_wasm_getglobal_number", L, "my_number")
local my_int = call("lua_wasm_getglobal_integer", L, "my_integer")
local my_str = call("lua_wasm_getglobal_string", L, "my_string")

print(string.format("[OK] my_number = %.2f", my_num))
print(string.format("[OK] my_integer = %d", my_int))
print(string.format("[OK] my_string = %s", my_str))

-- ==================== 测试表操作 ====================
print("\n--- 测试表操作 ---")

-- 创建表并设置字段
call("lua_wasm_dostring", L, "mytable = {a = 1, b = 2, c = 3}")

-- 获取字段
call("lua_wasm_getglobal", L, "mytable")
call("lua_wasm_getfield", L, -1, "a")
local field_a = call("lua_wasm_tonumber", L, -1)
call("lua_wasm_pop", L, 1)

call("lua_wasm_getfield", L, -1, "b")
local field_b = call("lua_wasm_tonumber", L, -1)
call("lua_wasm_pop", L, 1)

print(string.format("[OK] mytable.a = %.0f, mytable.b = %.0f", field_a, field_b))

-- ==================== 测试内存使用 ====================
print("\n--- 测试内存使用 ---")

local mem_usage = call("lua_wasm_memusage", L)
print(string.format("[OK] Lua 内存使用: %d bytes", mem_usage))

-- 执行垃圾回收
call("lua_wasm_collectgarbage", L)
print("[OK] 执行垃圾回收")

-- ==================== 关闭 Lua State ====================
print("\n--- 关闭 Lua State ---")

call("lua_wasm_close", L)
print("[OK] 关闭 Lua State")

-- ==================== 内存信息 ====================
print("\n--- 内存信息 ---")
local mem_size = runtime:getMemorySize()
print(string.format("[INFO] WASM 内存大小: %d bytes (%.2f MB)", mem_size, mem_size / 1024 / 1024))

-- ==================== 结果 ====================
print("\n" .. string.rep("=", 50))
print("=== lxclua.wasm 测试完成! ===")
print(string.rep("=", 50))
