-- 测试自定义调试输出回调函数

print("=== 自定义调试输出回调测试 ===\n")

-- 自定义的输出函数
local debug_messages = {}

local function my_debug_output(event, source, line)
    local msg = string.format("[DEBUG] 事件：%s | 文件：%s | 行号：%d", 
                              event, source, line)
    table.insert(debug_messages, msg)
    print(msg)  -- 这里可以替换成你自己的日志系统
end

-- 设置自定义输出回调
local old_callback = debug.setoutputcallback(my_debug_output)
print("已设置自定义输出回调")
print("之前的回调:", old_callback or "nil")
print()

-- 设置调试钩子
debug.sethook(function(event, line) end, "l")

-- 设置断点
print("设置断点...")
debug.setbreakpoint("test_custom_output.lua", 25)
debug.setbreakpoint("test_custom_output.lua", 30)
print()

-- 测试函数
local function test1()
    print("test1: 第 25 行")
    local x = 10
    local y = 20
    print("test1: 第 30 行")
    return x + y
end

local function test2()
    print("test2 运行")
    return "test2 结果"
end

-- 运行测试
print("=== 开始运行测试 ===")
local result1 = test1()
print("test1 结果:", result1)
print()

local result2 = test2()
print("test2 结果:", result2)
print()

-- 查看所有调试消息
print("=== 所有调试消息 ===")
for i, msg in ipairs(debug_messages) do
    print(string.format("%d. %s", i, msg))
end
print()

-- 测试获取当前回调
print("=== 测试获取回调 ===")
local current_callback = debug.getoutputcallback()
print("当前回调函数:", type(current_callback))
print()

-- 测试更改回调
print("=== 测试更改回调 ===")
local function another_callback(event, source, line)
    print(string.format("[LOG] %s:%d - %s", source, line, event))
end

local prev_callback = debug.setoutputcallback(another_callback)
print("已更改回调函数")
print("之前的回调类型:", type(prev_callback))
print()

-- 再次触发断点
debug.setbreakpoint("test_custom_output.lua", 53)
local function test3()
    print("test3: 新回调测试")
end
test3()
print()

-- 清除回调，使用默认输出
print("=== 测试清除回调（使用默认输出）===")
debug.setoutputcallback(nil)
print("已清除自定义回调，将使用默认输出")

-- 设置新断点测试默认输出
debug.setbreakpoint("test_custom_output.lua", 66)
local function test4()
    print("test4: 默认输出测试")
end
test4()
print()

-- 总结
print("=== 测试完成 ===")
print("\n可用的 API:")
print("  debug.setoutputcallback(callback)  - 设置自定义输出回调")
print("  debug.getoutputcallback()          - 获取当前输出回调")
print("\n回调函数参数:")
print("  callback(event, source, line)")
print("    - event: 事件类型（如 'breakpoint'）")
print("    - source: 源文件路径")
print("    - line: 行号")
