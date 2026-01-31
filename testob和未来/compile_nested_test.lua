-- 嵌套分发器CFF编译测试脚本
-- 测试新实现的OBFUSCATE_NESTED_DISPATCHER功能

local input_file = "test_nested_cff.lua"
local output_normal = "test_nested_cff_normal.luac"
local output_nested = "test_nested_cff_nested.luac"

-- 混淆标志位:
--   1  = OBFUSCATE_CFF              控制流扁平化
--   2  = OBFUSCATE_BLOCK_SHUFFLE    基本块随机打乱
--   4  = OBFUSCATE_BOGUS_BLOCKS     插入虚假基本块
--   8  = OBFUSCATE_STATE_ENCODE     状态值编码混淆
--   16 = OBFUSCATE_NESTED_DISPATCHER 嵌套分发器

print("======================================")
print("嵌套分发器CFF测试")
print("======================================")
print("")

-- 加载源文件
local f, err = loadfile(input_file)
if not f then
    print("错误: 无法加载 " .. input_file .. ": " .. tostring(err))
    return
end
print("源文件: " .. input_file)
print("")

-- 1. 先运行原始代码确认逻辑正确
print(">>> 运行原始代码:")
print("--------------------------------------")
f()
print("--------------------------------------")
print("")

-- 2. 编译为普通CFF混淆（标志=1）
print(">>> 编译普通CFF混淆 (标志=1)...")
local bc_normal = string.dump(f, {strip=false, obfuscate=1})
local out1 = io.open(output_normal, 'wb')
if out1 then
    out1:write(bc_normal)
    out1:close()
    print("已生成: " .. output_normal .. " (" .. #bc_normal .. " 字节)")
else
    print("错误: 无法创建 " .. output_normal)
end
print("")

-- 3. 编译为嵌套分发器CFF混淆（标志=1+16=17）
print(">>> 编译嵌套分发器CFF混淆 (标志=17)...")
local bc_nested = string.dump(f, {strip=false, obfuscate=17})
local out2 = io.open(output_nested, 'wb')
if out2 then
    out2:write(bc_nested)
    out2:close()
    print("已生成: " .. output_nested .. " (" .. #bc_nested .. " 字节)")
else
    print("错误: 无法创建 " .. output_nested)
end
print("")

-- 4. 加载并运行普通CFF混淆后的代码
print(">>> 运行普通CFF混淆后的代码:")
print("--------------------------------------")
local f_normal, err1 = loadfile(output_normal)
if f_normal then
    local ok, result = pcall(f_normal)
    if not ok then
        print("运行错误: " .. tostring(result))
    end
else
    print("加载错误: " .. tostring(err1))
end
print("--------------------------------------")
print("")

-- 5. 加载并运行嵌套分发器混淆后的代码
print(">>> 运行嵌套分发器混淆后的代码:")
print("--------------------------------------")
local f_nested, err2 = loadfile(output_nested)
if f_nested then
    local ok, result = pcall(f_nested)
    if not ok then
        print("运行错误: " .. tostring(result))
    end
else
    print("加载错误: " .. tostring(err2))
end
print("--------------------------------------")
print("")

-- 6. 编译全开混淆（标志=31）
print(">>> 编译全开混淆 (标志=31)...")
local output_full = "test_nested_cff_full.luac"
local bc_full = string.dump(f, {strip=false, obfuscate=31})
local out3 = io.open(output_full, 'wb')
if out3 then
    out3:write(bc_full)
    out3:close()
    print("已生成: " .. output_full .. " (" .. #bc_full .. " 字节)")
end
print("")

-- 7. 运行全开混淆后的代码
print(">>> 运行全开混淆后的代码:")
print("--------------------------------------")
local f_full, err3 = loadfile(output_full)
if f_full then
    local ok, result = pcall(f_full)
    if not ok then
        print("运行错误: " .. tostring(result))
    end
else
    print("加载错误: " .. tostring(err3))
end
print("--------------------------------------")
print("")

print("======================================")
print("测试完成!")
print("======================================")
