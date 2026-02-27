-- 测试断点调试功能的示例脚本

local function test_function(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i
        print("Current sum:", sum)
    end
    return sum
end

local function fibonacci(n)
    if n <= 1 then
        return n
    end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

print("=== 断点调试功能测试 ===")

-- 测试 1: 设置断点
print("\n1. 测试设置断点")
local bp1 = debug.setbreakpoint("test_breakpoint.lua", 7)
print("设置断点:", bp1)

-- 测试 2: 获取所有断点
print("\n2. 获取所有断点")
local breakpoints = debug.getbreakpoints()
for i, bp in ipairs(breakpoints) do
    print(string.format("断点 %d: %s:%d (启用：%s)", 
        i, bp.source, bp.line, tostring(bp.enabled)))
end

-- 测试 3: 设置带条件的断点
print("\n3. 设置带条件的断点")
local bp2 = debug.setbreakpoint("test_breakpoint.lua", 6, "i > 3")
print("设置条件断点:", bp2)

-- 测试 4: 禁用断点
print("\n4. 禁用断点")
debug.enablebreakpoint("test_breakpoint.lua", 7, false)
print("断点已禁用")

-- 测试 5: 重新启用断点
print("\n5. 重新启用断点")
debug.enablebreakpoint("test_breakpoint.lua", 7, true)
print("断点已启用")

-- 测试 6: 移除断点
print("\n6. 移除断点")
local removed = debug.removebreakpoint("test_breakpoint.lua", 6)
print("移除断点:", tostring(removed))

-- 测试 7: 清除所有断点
print("\n7. 清除所有断点")
local count = debug.clearbreakpoints()
print("清除了", count, "个断点")

-- 测试 8: 调试控制命令
print("\n8. 测试调试控制命令")
print("continue:", debug.continue())
print("step:", debug.step())
print("next:", debug.next())
print("finish:", debug.finish())

-- 实际运行函数测试
print("\n=== 实际运行测试 ===")
local result = test_function(5)
print("test_function(5) =", result)

print("\nfibonacci(10) =", fibonacci(10))

print("\n=== 测试完成 ===")
