-- 断点调试功能演示 - 实际使用示例

print("=== 断点调试功能演示 ===\n")

-- 启用调试钩子（需要行事件）
debug.sethook(function(event, line)
    -- 这里可以添加自定义的调试逻辑
end, "l")

-- 示例函数：计算阶乘
local function factorial(n)
    if n <= 1 then
        return 1
    end
    return n * factorial(n - 1)
end

-- 示例函数：冒泡排序
local function bubble_sort(arr)
    local n = #arr
    for i = 1, n - 1 do
        for j = 1, n - i do
            if arr[j] > arr[j + 1] then
                arr[j], arr[j + 1] = arr[j + 1], arr[j]
            end
        end
    end
    return arr
end

-- 示例函数：斐波那契数列
local function fibonacci(n)
    if n <= 1 then
        return n
    end
    return fibonacci(n - 1) + fibonacci(n - 2)
end

print("1. 设置断点并运行")
print("----------------------------------------")

-- 在 factorial 函数的第 13 行设置断点
local bp1 = debug.setbreakpoint("demo_breakpoint.lua", 13)
print("在 factorial 函数第 13 行设置断点")

-- 在 bubble_sort 的第 20 行设置条件断点（仅当 i=2 时触发）
local bp2 = debug.setbreakpoint("demo_breakpoint.lua", 20, "i == 2")
print("在 bubble_sort 第 20 行设置条件断点 (i == 2)")

print("\n2. 查看所有断点")
print("----------------------------------------")
local breakpoints = debug.getbreakpoints()
for i, bp in ipairs(breakpoints) do
    local cond = bp.condition or "无条件"
    print(string.format("断点 %d: %s:%d - %s (启用：%s)", 
        i, bp.source, bp.line, cond, tostring(bp.enabled)))
end

print("\n3. 运行代码（断点会在控制台输出）")
print("----------------------------------------")

-- 运行阶乘计算
print("\n计算 factorial(5):")
local result = factorial(5)
print("结果:", result)

-- 运行排序
print("\n运行冒泡排序:")
local arr = {5, 2, 8, 1, 9, 3}
print("排序前:", table.concat(arr, ", "))
bubble_sort(arr)
print("排序后:", table.concat(arr, ", "))

-- 运行斐波那契
print("\n计算 fibonacci(10):")
local fib = fibonacci(10)
print("结果:", fib)

print("\n4. 调试控制命令演示")
print("----------------------------------------")
print("debug.continue():", debug.continue())  -- 继续执行
print("debug.step():", debug.step())          -- 单步进入
print("debug.next():", debug.next())          -- 单步跳过
print("debug.finish():", debug.finish())      -- 执行到函数返回

print("\n5. 管理断点")
print("----------------------------------------")

-- 禁用断点
debug.enablebreakpoint("demo_breakpoint.lua", 13, false)
print("禁用第 13 行的断点")

-- 重新启用
debug.enablebreakpoint("demo_breakpoint.lua", 13, true)
print("重新启用第 13 行的断点")

-- 移除断点
debug.removebreakpoint("demo_breakpoint.lua", 20)
print("移除第 20 行的断点")

-- 清除所有断点
local count = debug.clearbreakpoints()
print("清除所有断点，共清除:", count, "个")

print("\n6. 再次运行代码（无断点）")
print("----------------------------------------")
print("factorial(6) =", factorial(6))
print("fibonacci(15) =", fibonacci(15))

print("\n=== 演示完成 ===")
print("\n断点调试功能已成功添加！")
print("\n可用的调试 API:")
print("  debug.setbreakpoint(source, line, [condition])  - 设置断点")
print("  debug.removebreakpoint(source, line)            - 移除断点")
print("  debug.getbreakpoints()                          - 获取所有断点")
print("  debug.enablebreakpoint(source, line, enable)    - 启用/禁用断点")
print("  debug.clearbreakpoints()                        - 清除所有断点")
print("  debug.continue()                                - 继续执行")
print("  debug.step()                                    - 单步进入")
print("  debug.next()                                    - 单步跳过")
print("  debug.finish()                                  - 执行到函数返回")
