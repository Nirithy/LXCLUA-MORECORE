-- CFF混淆编译脚本
-- 将源文件编译为混淆后的字节码文件
--
-- 混淆标志位:
--   1   = OBFUSCATE_CFF               控制流扁平化
--   2   = OBFUSCATE_BLOCK_SHUFFLE     基本块随机打乱
--   4   = OBFUSCATE_BOGUS_BLOCKS      插入虚假基本块
--   8   = OBFUSCATE_STATE_ENCODE      状态值编码混淆
--   16  = OBFUSCATE_NESTED_DISPATCHER 嵌套分发器（多层状态机）
--   32  = OBFUSCATE_OPAQUE_PREDICATES 不透明谓词（恒真/恒假条件）
--   64  = OBFUSCATE_FUNC_INTERLEAVE   函数交织（虚假函数路径）
--   128 = OBFUSCATE_VM_PROTECT        VM保护（自定义虚拟机指令集）
--
-- 用法: lua compile_cff.lua <输入文件> <输出文件> [混淆标志]
-- 示例: lua compile_cff.lua test.lua test.luac 255  -- 全开混淆

local input_file = arg[1] or "test_cff_demo.lua"
local output_file = arg[2] or "test_cff_demo.luac"
local obf_flags = tonumber(arg[3]) or 1  -- 默认仅CFF

-- 加载源文件
local f = loadfile(input_file)
if not f then
    print("错误: 无法加载 " .. input_file)
    return
end

-- 打印混淆配置
local flag_names = {}
if obf_flags & 1 ~= 0 then table.insert(flag_names, "CFF") end
if obf_flags & 2 ~= 0 then table.insert(flag_names, "SHUFFLE") end
if obf_flags & 4 ~= 0 then table.insert(flag_names, "BOGUS") end
if obf_flags & 8 ~= 0 then table.insert(flag_names, "STATE_ENCODE") end
if obf_flags & 16 ~= 0 then table.insert(flag_names, "NESTED") end
if obf_flags & 32 ~= 0 then table.insert(flag_names, "OPAQUE") end
if obf_flags & 64 ~= 0 then table.insert(flag_names, "INTERLEAVE") end
if obf_flags & 128 ~= 0 then table.insert(flag_names, "VM_PROTECT") end
print("混淆标志: " .. obf_flags .. " (" .. table.concat(flag_names, "+") .. ")")

-- 导出混淆字节码
local bc = string.dump(f, {strip=false, obfuscate=obf_flags})

-- 写入输出文件
local out = io.open(output_file, 'wb')
if out then
    out:write(bc)
    out:close()
    print("混淆字节码已写入: " .. output_file)
else
    print("错误: 无法创建 " .. output_file)
end
