-- 测试默认输出

print("开始")

-- 设置钩子
debug.sethook(function() end, "l")

-- 清除回调，使用默认输出
debug.setoutputcallback(nil)

print("设置断点在第 13 行")
debug.setbreakpoint("test_default.lua", 13)

-- 运行
print("运行")
local x = 10  -- 第 13 行
print("x =", x)

print("完成")
