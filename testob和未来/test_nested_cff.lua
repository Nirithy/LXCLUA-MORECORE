-- 嵌套分发器CFF测试文件
-- 包含多个分支，用于测试控制流扁平化

local function calculate(x, y, op)
    local result = 0
    
    if op == "add" then
        result = x + y
    elseif op == "sub" then
        result = x - y
    elseif op == "mul" then
        result = x * y
    elseif op == "div" then
        if y ~= 0 then
            result = x / y
        else
            result = 0
        end
    else
        result = x
    end
    
    return result
end

local function grade(score)
    local level
    if score >= 90 then
        level = "A"
    elseif score >= 80 then
        level = "B"
    elseif score >= 70 then
        level = "C"
    elseif score >= 60 then
        level = "D"
    else
        level = "F"
    end
    return level
end

-- 测试代码
print("=== 嵌套分发器CFF测试 ===")
print("10 + 5 = " .. calculate(10, 5, "add"))
print("10 - 5 = " .. calculate(10, 5, "sub"))
print("10 * 5 = " .. calculate(10, 5, "mul"))
print("10 / 5 = " .. calculate(10, 5, "div"))
print("10 / 0 = " .. calculate(10, 0, "div"))
print("")
print("成绩 95: " .. grade(95))
print("成绩 85: " .. grade(85))
print("成绩 75: " .. grade(75))
print("成绩 65: " .. grade(65))
print("成绩 55: " .. grade(55))
print("")
print("=== 测试完成 ===")
