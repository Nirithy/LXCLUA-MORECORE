# TCC 混淆与加密使用指南

本指南将详细介绍如何使用 TCC 库开启混淆功能，尤其是字符串加密。

## 基本使用方法

TCC 库通过 `tcc.compile` 函数进行代码编译和混淆。基本语法如下：

```lua
local tcc = require("tcc")

local c_code = tcc.compile(lua_code, module_name, options)
```

- `lua_code`: (string) 需要编译的 Lua 源代码。
- `module_name`: (string) 生成的 C 模块名称（对应 `luaopen_module_name`）。
- `options`: (table) 配置选项表。

## 开启混淆 (Obfuscation)

要开启一般的代码混淆（如 API 接口混淆），需要在 `options` 表中设置 `obfuscate = true`。

```lua
local options = {
    obfuscate = true
}
```

开启此选项后，生成的 C 代码将通过间接调用的方式访问 Lua API，并使用随机种子打乱函数指针表，增加逆向难度。

## 开启字符串加密 (String Encryption)

要开启字符串加密，需要在 `options` 表中设置 `string_encryption = true`。

```lua
local options = {
    string_encryption = true
}
```

开启后，Lua 源代码中的字符串常量将在生成的 C 代码中被加密存储（使用 XOR 算法和动态密钥），并在运行时动态解密。这能有效防止通过 `strings` 命令直接查看敏感信息。

**注意：** 字符串加密仅针对代码中的字符串常量，不包括表键名（除非它是字符串常量且被引用）。

## 其他混淆选项

除了上述两个主要选项外，TCC 还支持多种细粒度的混淆控制：

| 选项名 | 类型 | 描述 |
| :--- | :--- | :--- |
| `flatten` | boolean | 开启控制流平坦化 (Control Flow Flattening)，打乱基本块的执行顺序。 |
| `block_shuffle` | boolean | 随机打乱基本块的物理顺序。 |
| `bogus_blocks` | boolean | 插入虚假的基本块（死代码）。 |
| `state_encode` | boolean | 对状态变量进行编码。 |
| `nested_dispatcher` | boolean | 使用嵌套的调度器结构。 |
| `opaque_predicates` | boolean | 使用不透明谓词（可能增加运行时开销，需谨慎使用）。 |
| `func_interleave` | boolean | 函数体交叉混淆。 |
| `vm_protect` | boolean | 启用虚拟机保护（解释器混淆）。 |
| `binary_dispatcher` | boolean | 使用二分查找调度器。 |
| `random_nop` | boolean | 插入随机的 NOP 指令。 |
| `seed` | integer | 混淆使用的随机种子。若不指定，默认为当前时间戳。 |

## 完整示例

以下是一个同时开启代码混淆、控制流平坦化和字符串加密的完整示例：

```lua
local tcc = require("tcc")

-- 待编译的 Lua 代码
local source_code = [[
    local secret = "MySecretKey123"
    print("正在处理机密数据: " .. secret)

    local function calculate(a, b)
        if a > b then
            return a - b
        else
            return a + b
        end
    end

    print("结果: ", calculate(10, 20))
]]

-- 编译选项
local options = {
    modname = "my_secure_module", -- 模块名
    obfuscate = true,             -- 开启接口混淆
    string_encryption = true,     -- 开启字符串加密
    flatten = true,               -- 开启控制流平坦化
    block_shuffle = true,         -- 开启基本块乱序
    seed = 123456                 -- 固定种子（可选，用于复现）
}

-- 编译为 C 代码
local c_code = tcc.compile(source_code, "my_secure_module", options)

-- 将生成的 C 代码保存到文件
local f = io.open("my_secure_module.c", "w")
f:write(c_code)
f:close()

print("编译完成！生成的 C 代码已保存至 my_secure_module.c")
```

生成的 C 代码需要使用 GCC 或 Clang 编译为共享库（.so 或 .dll），并在 Lua 中通过 `require "my_secure_module"` 加载。
