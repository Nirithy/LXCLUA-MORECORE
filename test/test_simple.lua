-- 测试自定义调试输出回调函数 - 更简单的示例

print("=== 自定义调试输出回调测试 ===\n")

-- 1. 测试默认输出（无回调）
print("1. 测试默认输出")
print("----------------------------------------")

-- 设置调试钩子（必须设置钩子才能触发断点）
debug.sethook(function(event, line) end, "l")

-- 设置断点
local bp1 = debug.setbreakpoint("test_simple.lua", 15)
print("已设置断点在第 15 行\n")

-- 运行到断点
print("运行到断点:")
local x = 10
local y = 20  -- 第 15 行
local z = x + y
print("结果:", z, "\n")

-- 2. 设置自定义回调
print("2. 设置自定义回调函数")
print("----------------------------------------")

local debug_log = {}

local function my_callback(event, source, line)
    local msg = string.format("[DEBUG %s] %s:%d", event, source, line)
    table.insert(debug_log, msg)
    print(msg)
end

local old_cb = debug.setoutputcallback(my_callback)
print("已设置自定义回调")
print("之前的回调:", type(old_cb), "\n")

-- 设置新断点
debug.setbreakpoint("test_simple.lua", 35)
print("设置新断点在第 35 行\n")

-- 运行
print("运行代码:")
function test_func()
    local a = 1
    local b = 2
    local c = 3  -- 第 35 行
    return a + b + c
end
local result = test_func()
print("结果:", result, "\n")

-- 3. 查看所有调试日志
print("3. 查看所有调试日志")
print("----------------------------------------")
for i, log in ipairs(debug_log) do
    print(string.format("%d. %s", i, log))
end
print()

-- 4. 测试获取回调
print("4. 测试获取回调")
print("----------------------------------------")
local current = debug.getoutputcallback()
print("当前回调类型:", type(current), "\n")

-- 5. 测试更改回调
print("5. 测试更改回调")
print("----------------------------------------")
local function new_callback(event, source, line)
    print(string.format(">>> 新回调：[%s] %s:%d", event, source, line))
end

debug.setoutputcallback(new_callback)
debug.setbreakpoint("test_simple.lua", 60)

print("设置新回调并运行:")
local function another_func()
    local val = 100  -- 第 60 行
    print("val =", val)
end
another_func()
print()

-- 6. 测试清除回调（使用默认输出）
print("6. 测试清除回调（使用默认输出）")
print("----------------------------------------")
debug.setoutputcallback(nil)
debug.setbreakpoint("test_simple.lua", 72)
print("已清除回调，使用默认输出:")

local function final_func()
    local msg = "hello"  -- 第 72 行
    print(msg)
end
final_func()
print()

-- 总结
print("=== 测试完成 ===")
print("\n新增的 API:")
print("  debug.setoutputcallback(callback)  - 设置自定义输出回调函数")
print("  debug.getoutputcallback()          - 获取当前输出回调函数")
print("\n回调函数签名:")
print("  callback(event, source, line)")
print("    - event: 事件类型 ('breakpoint', 'step', 等)")
print("    - source: 源文件路径")
print("    - line: 行号")
print("\n示例:")
print([[
  debug.setoutputcallback(function(event, source, line)
      print("断点:", source, line)
  end)
]])
