-- 最简单的断点测试

print("开始")

-- 设置钩子
debug.sethook(function() end, "l")

-- 设置断点（第 14 行是可执行代码）
print("设置断点")
debug.setbreakpoint("test_minimal.lua", 14)

-- 运行
print("运行")
local x = 10  -- 第 14 行
print("x =", x)

print("完成")
