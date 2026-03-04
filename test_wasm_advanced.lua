-- wasm3 C语言高级功能测试
-- 运行: lxclua test_wasm_advanced.lua

local wasm3 = require("wasm3")

print("=== wasm3 C语言高级功能测试 ===\n")

-- 创建环境
local env = wasm3.newEnvironment()
local runtime = env:newRuntime(2 * 1024 * 1024)  -- 2MB 栈

-- 读取 WASM 文件
local f = io.open("test_wasm_advanced.wasm", "rb")
if not f then
    print("[错误] 请先编译: make wasm-c-all SRC=test_wasm_advanced.c")
    return
end
local wasm_data = f:read("*a")
f:close()

-- 解析模块
local module = env:parseModule(wasm_data)

-- 加载模块
runtime:loadModule(module)

-- 辅助函数
local function call(name, ...)
    local func = runtime:findFunction(name)
    return func:call(...)
end

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
print()

-- ==================== 位操作 ====================
print("--- 位操作 ---")

all_passed = test("popcount(255)", call("popcount", 255), 8) and all_passed
all_passed = test("popcount(0xAA)", call("popcount", 0xAA), 4) and all_passed
all_passed = test("rotl(1, 4)", call("rotl", 1, 4), 16) and all_passed
all_passed = test("rotr(16, 4)", call("rotr", 16, 4), 1) and all_passed
print()

-- ==================== 递归算法 ====================
print("--- 递归算法 ---")

all_passed = test("hanoi_moves(3)", call("hanoi_moves", 3), 7) and all_passed
all_passed = test("hanoi_moves(10)", call("hanoi_moves", 10), 1023) and all_passed
all_passed = test("combination(5, 2)", call("combination", 5, 2), 10) and all_passed
all_passed = test("combination(10, 5)", call("combination", 10, 5), 252) and all_passed
all_passed = test("pascal(6, 3)", call("pascal", 6, 3), 20) and all_passed
print()

-- ==================== 数列 ====================
print("--- 数列 ---")

all_passed = test("fib(10)", call("fib", 10), 55) and all_passed
all_passed = test("fib(20)", call("fib", 20), 6765) and all_passed
all_passed = test("lucas(10)", call("lucas", 10), 123) and all_passed
all_passed = test("pell(10)", call("pell", 10), 2378) and all_passed
all_passed = test("triangular(10)", call("triangular", 10), 55) and all_passed
all_passed = test("pentagonal(10)", call("pentagonal", 10), 145) and all_passed
all_passed = test("hexagonal(10)", call("hexagonal", 10), 190) and all_passed
print()

-- ==================== 结构体操作 ====================
print("--- 结构体操作 ---")

-- Point 结构体测试 - 使用分开的坐标参数函数
local dist_sq = call("point_distance_sq_xy", 3, 4, 0, 0)
all_passed = test("point_distance_sq(3,4)-(0,0)", dist_sq, 25) and all_passed

local manhattan = call("point_manhattan_xy", 3, 4, 0, 0)
all_passed = test("point_manhattan(3,4)-(0,0)", manhattan, 7) and all_passed

local dist = call("point_distance_xy", 3, 4, 0, 0)
print(string.format("[OK] point_distance(3,4)-(0,0) = %.2f", dist))

-- Rectangle 结构体测试
local area = call("rect_area_wh", 10, 20)
all_passed = test("rect_area(10x20)", area, 200) and all_passed

local perimeter = call("rect_perimeter_wh", 10, 20)
all_passed = test("rect_perimeter(10x20)", perimeter, 60) and all_passed

local contains = call("rect_contains_point_wh", 10, 20, 5, 5)
all_passed = test("rect_contains(5,5)", contains, 1) and all_passed

local not_contains = call("rect_contains_point_wh", 10, 20, 15, 5)
all_passed = test("rect_contains(15,5)", not_contains, 0) and all_passed
print()

-- ==================== 内存操作 ====================
print("--- 内存操作 ---")

local mem_size = runtime:getMemorySize()
print(string.format("[INFO] 内存大小: %d bytes", mem_size))

-- 使用 WASM 函数读写内存
local test_offset = 1024

-- 写入并读取 int32
call("write_i32", test_offset, 0, 12345)
local val = call("read_i32", test_offset, 0)
all_passed = test("memory write/read i32", val, 12345) and all_passed

-- 写入并读取 int64
call("write_i64", test_offset, 0, 0x123456789ABCDEF0)
local val64 = call("read_i64", test_offset, 0)
print(string.format("[OK] memory write/read i64: 0x%X", val64))

-- 写入并读取 float
call("write_f32", test_offset, 0, 3.14159)
local f32 = call("read_f32", test_offset, 0)
print(string.format("[OK] memory write/read f32: %.5f", f32))

-- 写入并读取 double
call("write_f64", test_offset, 0, 2.718281828)
local f64 = call("read_f64", test_offset, 0)
print(string.format("[OK] memory write/read f64: %.9f", f64))
print()

-- ==================== 数组操作 ====================
print("--- 数组操作 ---")

-- 在内存中创建数组
local arr_offset = 2048
local arr_len = 5
local arr_data = {10, 20, 30, 40, 50}

-- 写入数组
for i, v in ipairs(arr_data) do
    call("write_i32", arr_offset, i - 1, v)
end

-- 测试数组函数
local sum = call("array_sum", arr_offset, arr_len)
all_passed = test("array_sum({10,20,30,40,50})", sum, 150) and all_passed

local max = call("array_max", arr_offset, arr_len)
all_passed = test("array_max({10,20,30,40,50})", max, 50) and all_passed

local min = call("array_min", arr_offset, arr_len)
all_passed = test("array_min({10,20,30,40,50})", min, 10) and all_passed

-- 数组反转
call("array_reverse", arr_offset, arr_len)
print("[OK] 数组反转后:")
for i = 0, arr_len - 1 do
    io.write(string.format("  arr[%d]=%d", i, call("read_i32", arr_offset, i)))
end
print()

-- 恢复数组
for i, v in ipairs(arr_data) do
    call("write_i32", arr_offset, i - 1, v)
end

-- 快速排序测试
local unsorted_offset = 2100
local unsorted = {64, 34, 25, 12, 22, 11, 90}
for i, v in ipairs(unsorted) do
    call("write_i32", unsorted_offset, i - 1, v)
end
call("array_sort_quick", unsorted_offset, 7)
print("[OK] 快速排序后:")
for i = 0, 6 do
    io.write(string.format("%d ", call("read_i32", unsorted_offset, i)))
end
print()

-- 二分查找
local found = call("array_binary_search", unsorted_offset, 7, 25)
all_passed = test("binary_search(25)", found, 3) and all_passed  -- 排序后 25 在索引 3
print()

-- ==================== 矩阵操作 ====================
print("--- 矩阵操作 ---")

-- 创建 3x3 矩阵
local mat_offset = 4096
for i = 0, 8 do
    call("write_i32", mat_offset, i, i + 1)
end

print("[OK] 3x3 矩阵:")
for row = 0, 2 do
    io.write("  [")
    for col = 0, 2 do
        local val = call("matrix_get", mat_offset, 3, row, col)
        io.write(string.format("%2d", val))
        if col < 2 then io.write(", ") end
    end
    io.write("]\n")
end

local mat_sum = call("matrix_sum", mat_offset, 3, 3)
all_passed = test("matrix_sum", mat_sum, 45) and all_passed

local trace = call("matrix_trace", mat_offset, 3)
all_passed = test("matrix_trace", trace, 15) and all_passed

local det = call("matrix_det_3x3", mat_offset)
print(string.format("[OK] 行列式 det = %d", det))
print()

-- ==================== 高级算法 ====================
print("--- 高级算法 ---")

-- 最长递增子序列
local lis_offset = 2300
local lis_data = {10, 22, 9, 33, 21, 50, 41}
for i, v in ipairs(lis_data) do
    call("write_i32", lis_offset, i - 1, v)
end
local lis = call("lis_length", lis_offset, 7)
all_passed = test("lis_length({10,22,9,33,21,50,41})", lis, 4) and all_passed  -- 正确答案是 4

-- 最大子数组和
local msa_offset = 2400
local msa_data = {-2, 1, -3, 4, -1, 2, 1, -5, 4}
for i, v in ipairs(msa_data) do
    call("write_i32", msa_offset, i - 1, v)
end
local msa = call("max_subarray_sum", msa_offset, 9)
all_passed = test("max_subarray_sum", msa, 6) and all_passed
print()

-- ==================== 结果 ====================
print("\n" .. string.rep("=", 50))
if all_passed then
    print("=== 所有测试通过! ===")
else
    print("=== 部分测试失败! ===")
end
print(string.rep("=", 50))
