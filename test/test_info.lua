-- 详细测试

print("开始")

-- 设置钩子
debug.sethook(function() end, "l")

-- 清除回调
debug.setoutputcallback(nil)

-- 获取当前脚本信息
local info = debug.getinfo(1)
print("脚本 source:", info.source)
print("脚本 short_src:", info.short_src)

-- 设置断点
local script_path = info.source
if script_path:sub(1, 1) == "@" then
    script_path = script_path:sub(2)
end
print("设置断点在:", script_path, 17)
debug.setbreakpoint(script_path, 17)

-- 运行
print("运行")
local x = 10  -- 第 17 行
print("x =", x)

print("完成")
