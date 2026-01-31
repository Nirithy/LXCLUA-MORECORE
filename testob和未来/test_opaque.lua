-- 不透明谓词CFF测试脚本

local input_file = "test_nested_cff.lua"

print("======================================")
print("不透明谓词CFF测试")
print("======================================")
print("")

local f, err = loadfile(input_file)
if not f then
    print("错误: 无法加载 " .. input_file .. ": " .. tostring(err))
    return
end

-- 先运行原始代码
print(">>> 运行原始代码:")
print("--------------------------------------")
f()
print("--------------------------------------")
print("")

-- 测试各种混淆组合
local tests = {
    {flags = 1,  name = "CFF基础"},
    {flags = 33, name = "CFF+不透明谓词"},         -- 1+32
    {flags = 49, name = "CFF+嵌套+不透明谓词"},    -- 1+16+32
    {flags = 63, name = "全开混淆"},               -- 1+2+4+8+16+32
}

for _, test in ipairs(tests) do
    local output = "test_opaque_" .. test.flags .. ".luac"
    
    print(">>> 编译: " .. test.name .. " (标志=" .. test.flags .. ")...")
    local bc = string.dump(f, {strip=false, obfuscate=test.flags})
    
    local out = io.open(output, 'wb')
    if out then
        out:write(bc)
        out:close()
        print("已生成: " .. output .. " (" .. #bc .. " 字节)")
    end
    
    print("运行混淆后代码:")
    print("--------------------------------------")
    local f2, err2 = loadfile(output)
    if f2 then
        local ok, result = pcall(f2)
        if not ok then
            print("运行错误: " .. tostring(result))
        end
    else
        print("加载错误: " .. tostring(err2))
    end
    print("--------------------------------------")
    print("")
end

print("======================================")
print("测试完成!")
print("======================================")
