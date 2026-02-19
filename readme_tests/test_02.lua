name = "World"
age = 25

-- 简单变量插值
print("Hello, ${name}!")  -- Hello, World!

-- 表达式插值
print("Next year: ${[age + 1]}")  -- Next year: 26

-- 原始字符串 (Raw String)
local path = _raw"C:\Windows\System32"  -- C:\Windows\System32
