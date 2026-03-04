-- wasm3 复杂功能测试脚本
-- 运行: lxclua test_wasm_complex.lua

local wasm3 = require("wasm3")

print("=== wasm3 复杂功能测试 ===\n")

-- 创建环境
local env = wasm3.newEnvironment()
local runtime = env:newRuntime(1024 * 1024)  -- 1MB 栈

-- 读取 WASM 文件
local f, err = io.open("test_wasm_complex.wasm", "rb")
if not f then
    print("[错误] 请先编译: make wasm-c-all SRC=test_wasm_complex.c")
    return
end
local wasm_data = f:read("*a")
f:close()

-- 解析模块
local module = env:parseModule(wasm_data)

-- 加载模块
runtime:loadModule(module)

-- 辅助函数：调用 WASM 函数
local function call(name, ...)
    local func = runtime:findFunction(name)
    return func:call(...)
end

-- 辅助函数：打印测试结果
local function test(name, got, expected)
    local ok = got == expected
    local status = ok and "[OK]" or "[FAIL]"
    print(string.format("%s %s: got=%s, expected=%s", status, name, tostring(got), tostring(expected)))
    return ok
end

local all_passed = true

-- ==================== 数学运算 ====================
print("--- 数学运算 ---")

all_passed = test("power(2, 10)", call("power", 2, 10), 1024) and all_passed
all_passed = test("power(3, 5)", call("power", 3, 5), 243) and all_passed
all_passed = test("gcd(48, 18)", call("gcd", 48, 18), 6) and all_passed
all_passed = test("gcd(100, 35)", call("gcd", 100, 35), 5) and all_passed
all_passed = test("lcm(12, 18)", call("lcm", 12, 18), 36) and all_passed
all_passed = test("is_prime(17)", call("is_prime", 17), 1) and all_passed
all_passed = test("is_prime(18)", call("is_prime", 18), 0) and all_passed
all_passed = test("nth_prime(10)", call("nth_prime", 10), 29) and all_passed
all_passed = test("int_sqrt(100)", call("int_sqrt", 100), 10) and all_passed
all_passed = test("int_sqrt(99)", call("int_sqrt", 99), 9) and all_passed

-- ==================== 位操作 ====================
print("\n--- 位操作 ---")

all_passed = test("popcount(255)", call("popcount", 255), 8) and all_passed
all_passed = test("popcount(0xAA)", call("popcount", 0xAA), 4) and all_passed
all_passed = test("rotl(1, 4)", call("rotl", 1, 4), 16) and all_passed
all_passed = test("rotr(16, 4)", call("rotr", 16, 4), 1) and all_passed

-- ==================== 递归算法 ====================
print("\n--- 递归算法 ---")

all_passed = test("hanoi_moves(3)", call("hanoi_moves", 3), 7) and all_passed
all_passed = test("hanoi_moves(10)", call("hanoi_moves", 10), 1023) and all_passed
all_passed = test("combination(5, 2)", call("combination", 5, 2), 10) and all_passed
all_passed = test("combination(10, 5)", call("combination", 10, 5), 252) and all_passed
all_passed = test("pascal(6, 3)", call("pascal", 6, 3), 20) and all_passed

-- ==================== 内存信息 ====================
print("\n--- 内存信息 ---")
local mem_size = runtime:getMemorySize()
print(string.format("[INFO] 内存大小: %d bytes (%.2f MB)", mem_size, mem_size / 1024 / 1024))

-- ==================== 结果 ====================
print("\n" .. string.rep("=", 40))
if all_passed then
    print("=== 所有测试通过! ===")
else
    print("=== 部分测试失败! ===")
end
print(string.rep("=", 40))
