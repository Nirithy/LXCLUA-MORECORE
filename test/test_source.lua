-- 测试断点输出 - 使用正确的源文件路径

print("=== 断点输出测试 ===\n")

-- 设置调试钩子
debug.sethook(function(event, line) end, "l")

-- 自定义回调
local function my_callback(event, source, line)
    print(string.format(">>> [DEBUG] %s | %s:%d", event, source, line))
end

debug.setoutputcallback(my_callback)

-- 获取当前脚本的实际路径
local info = debug.getinfo(1)
print("当前脚本的 source:", info.source)
print()

-- 设置断点 - 使用 info.source 中的路径
-- 注意：source 可能是 "@filename" 或者完整路径
local source = info.source
if source:sub(1, 1) == "@" then
    source = source:sub(2)  -- 移除 @ 前缀
end

print("设置断点...")
debug.setbreakpoint(source, 19)
print("断点已设置:", source, 19)
print()

-- 运行到断点
print("运行代码:")
local function test()
    local x = 10  -- 第 19 行
    print("x =", x)
end

test()
print()

print("=== 测试完成 ===")
