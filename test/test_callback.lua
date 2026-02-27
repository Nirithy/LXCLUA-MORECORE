-- 测试自定义输出回调

print("开始")

-- 设置钩子
debug.sethook(function() end, "l")

-- 设置自定义输出回调
debug.setoutputcallback(function(event, source, line)
    print(string.format(">>> [%s] %s:%d", event, source, line))
end)

print("设置断点在第 16 行")
debug.setbreakpoint("test_callback.lua", 16)

-- 运行
print("运行")
local x = 10  -- 第 16 行
print("x =", x)

print("完成")
