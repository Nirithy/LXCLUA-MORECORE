# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

[English](README_EN.md) | 中文

基于 **Lua 5.5 (Custom)** 深度定制的高性能嵌入式脚本引擎，提供增强的安全特性、扩展库支持和优化的字节码编译。

## 测试平台

| 平台 | 状态 | 字节码互通 |
|------|------|-----------|
| Windows (MinGW) | ✅ 通过 | ✅ |
| Arch Linux | ✅ 通过 | ✅ |
| Ubuntu | ✅ 通过 | ✅ |
| Android (Termux) | ✅ 通过 | ✅ |
| Android (LXCLUA JNI) | ✅ 通过 | ✅ |

## 特性

### 核心增强

- **安全编译** - 字节码采用动态 OPcode 映射、时间戳加密、SHA-256 完整性验证
- **PNG 封装** - 编译后的字节码嵌入 PNG 图像格式，增强混淆效果
- **防逆向保护** - 多层加密机制有效防止字节码被反编译或篡改
- **虚拟化保护** - 提供 `vmprotect` 库，支持将函数转换为虚拟化代码执行，隐藏原始逻辑
- **自定义 VM** - 实现 XCLUA 指令集与解释器，增强执行流混淆

### 扩展模块

| 模块 | 功能描述 |
|------|----------|
| `json` | 内置 JSON 解析与序列化支持 |
| `sha256` | SHA-256 哈希计算 |
| `lclass` | 面向对象编程支持（类、继承、多态） |
| `lbitlib` | 位运算库 |
| `lboolib` | 布尔类型增强 |
| `ludatalib` | 二进制数据序列化 |
| `lsmgrlib` | 内存管理工具 |
| `ltranslator` | 代码翻译器 |
| `logtable` | 日志表支持 |
| `lptrlib` | 指针操作库（支持类型化索引与算术运算） |
| `lvmlib` | 虚拟机扩展接口 |
| `process` | 进程管理与内存读写 (Linux only) |
| `http` | HTTP 客户端/服务端与 Socket 支持 |
| `vmprotect` | Lua 字节码虚拟化保护 |
| `thread` | 多线程支持 (创建、通信、同步) |
| `fs` | 文件系统操作 (ls, mkdir, rm, stat 等) |
| `struct` | C 风格结构体与强类型数组支持 |

### 编译优化

- 使用 C23 标准编译
- LTO（链接时优化）
- 循环展开与严格别名分析
- 去除调试符号，最小化二进制体积

## 语法扩展与特性

LXCLUA-NCore 引入了大量现代语言特性和语法糖，极大地扩展了 Lua 5.5 的能力。

### 1. 扩展运算符 (Extended Operators)

- **复合赋值**: `+=`, `-=`, `*=`, `/=`, `//=`, `%=`, `&=`, `|=`, `~=`, `>>=`, `<<=`, `..=`
- **自增**: `++` (e.g. `i++` 后缀自增)
- **比较**: `!=` (等价于 `~=`), `<=>` (三路比较)

```lua
local a = 10
a += 5  -- a = 15
a++     -- a = 16
```
- **管道操作**:
    - `|>`: 前向管道 (e.g. `x |> f` 等价于 `f(x)`)
    - `<|`: 反向管道 (e.g. `f <| x` 等价于 `f(x)`)
    - `|?>`: 安全管道 (e.g. `x |?> f`, 若 `x` 为 `nil` 则返回 `nil`)
- **空值处理**:
    - `??`: 空值合并 (e.g. `a ?? b` 等价于 `a == nil and b or a`)
    - `?.`: 可选链 (e.g. `obj?.prop`, 若 `obj` 为 `nil` 则不访问 `prop`)

### 2. 字符串增强 (Enhanced Strings)

- **插值字符串**: 使用 `"` 或 `'` 包裹，支持 `${var}` 和 `${[expr]}`。
    - 示例: `"Hello, ${name}!"`, `"Result: ${[a + b]}"`
- **原生字符串**: 使用 `_raw` 前缀，不处理转义字符。
    - 示例: `_raw"Path\to\file"`, `_raw[[...]]`

### 3. 函数特性 (Function Features)

- **箭头函数**: `(args) => expr` 或 `(args) => { stat }`
    - 示例: `map(list, (x) => x * 2)`
    - 语句形式: `-> { ... }` (等价于 `function() ... end`)
- **Lambda 表达式**: `lambda(args) expr` 或 `lambda(args) => stat`

```lua
-- 箭头函数
local f = (x) => x * 2
print(f(10)) -- 20

-- Lambda 表达式
local l = lambda(x, y): x + y
print(l(10, 20)) -- 30
```

- **泛型函数**: `function<T>(...)` 或 `function(...) requires expr`
- **Async/Await**: `async function foo() ... end`, `await task`
- **C 风格定义**: `int add(int a, int b) { return a + b; }`

### 4. 面向对象编程 (OOP)

支持完整的类和接口系统：

- **类定义**: `class Name [extends Base] [implements Iface] ... end`
- **成员修饰符**: `public`, `private`, `protected`, `static`, `abstract`, `final`, `sealed`
- **属性访问器**: `get prop() ... end`, `set prop(v) ... end`
- **实例化**: `new Class(...)` 或 `onew Class(...)`
- **父类访问**: `super.method(...)` 或 `osuper:method(...)`

```lua
class Point
    public x = 0
    public y = 0

    function __init__(x, y)
        self.x = x
        self.y = y
    end

    function move(dx, dy)
        self.x += dx
        self.y += dy
    end
end

local p = new Point(10, 20)
p:move(5, 5)
print(p.x, p.y) -- 15, 25
```

### 5. 结构体与类型 (Structs & Types)

- **结构体**: `struct Name { Type field; ... }`
- **泛型结构体**: `struct Box(T) { T value; }`
- **超级结构体**: `superstruct Name [ key: value, ... ]` (元数据/原型)

```lua
struct Vector3 {
    float x;
    float y;
    float z;
}

local v = Vector3()
v.x = 1.0
v.y = 2.0
v.z = 3.0
```
- **枚举**: `enum Color { Red, Green, Blue }`
- **类型标注**: 支持 `int`, `float`, `bool`, `string`, `void`, `char`, `long` 等。

### 6. 控制流扩展 (Control Flow)

- **Switch**: `switch (exp) { case v: ... }` 或 `switch (exp) do case ... end`
    - 支持作为表达式: `local x = switch(v) case 1: 10 case 2: 20 end`

```lua
switch(val) do
    case 1:
        print("Case 1")
    default:
        print("Default")
end

-- 表达式形式 (需要显式 return)
local res = switch(val) do
    case 1: return "One"
    case 2: return "Two"
end
```

- **Try-Catch**: `try ... catch(e) ... finally ... end`
- **Defer**: `defer statement` 或 `defer do ... end` (作用域结束时执行)

```lua
do
    defer print("Cleanup")
    print("Working...")
end
-- Output: Working... Cleanup
```

- **Namespace**: `namespace Name { ... }`, `using namespace Name;`

```lua
namespace MyLib {
    int value = 42;
}
print(MyLib.value)
```
- **Using**: `using Name::Member;`
- **Continue**: `continue` 关键字
- **When**: `when cond then ... case cond2 then ... else ... end`

### 7. 元编程 (Metaprogramming)

- **自定义命令**: `command Name(args) ... end`
- **自定义关键字**: `keyword Name(args) ... end`
- **自定义运算符**: `operator op (args) ... end` (支持 `++`, `**` 等)
- **宏调用**: `$Name(args)` 或 `$Name`

### 8. 内联汇编 (Inline ASM)

支持 `asm` 块直接编写虚拟机指令：

```lua
asm(
    LOADI 0 100  ; R[0] = 100
    RETURN 0 1
)
```

支持伪指令: `newreg`, `def`, `_if`, `junk` 等。

### 9. 预处理器 (Preprocessor)

支持类 C 预处理指令 (使用 `$` 前缀):
- `$if`, `$else`, `$elseif`, `$end`
- `$define`, `$include`, `$alias`
- `$haltcompiler`

## 系统要求

- **编译器**: GCC（支持 C11/C23 标准）
- **平台**: Windows (MinGW) / Linux / Android (Termux)

## 快速开始

### 编译

```bash
# Windows (MinGW)
make mingw

# Linux
make linux

# Android (Termux)
make termux
```

### 验证安装

```bash
make test
```

### 清理构建

```bash
make clean
```

## 构建产物

| 文件 | 描述 |
|------|------|
| `liblua.a` / `lua55.dll` | Lua 静态库 / 动态库 |
| `lua` / `lua.exe` | Lua 解释器 |
| `luac` / `luac.exe` | Lua 字节码编译器 |

## 使用示例

### 运行 Lua 脚本

```bash
./lua script.lua
```

### 编译为字节码

```bash
./luac -o output.luac script.lua
```

### 嵌入到 C/C++ 项目

```c
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int main() {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    
    luaL_dofile(L, "script.lua");
    
    lua_close(L);
    return 0;
}
```

## 项目结构

```
.
├── l*.c / l*.h      # Lua 核心源码
├── json_parser.*    # JSON 解析模块
├── sha256.*         # SHA-256 哈希模块
├── lclass.*         # 面向对象类系统
├── stb_image*.h     # 图像处理（stb 库）
├── Makefile         # 构建脚本
└── lua/             # 子版本（次逻辑）
```

## 安全说明

本项目的字节码编译采用多重安全机制：

1. **动态 OPcode 映射** - 每次编译生成唯一的指令映射表
2. **时间戳加密** - 使用编译时间作为加密密钥
3. **SHA-256 验证** - 确保字节码完整性
4. **PNG 图像封装** - 将加密数据嵌入图像格式

> 注意：这些保护措施旨在增加逆向工程难度，但不能保证绝对安全。

## 许可证

本项目基于 [MIT 许可证](LICENSE) 开源。

Lua 原始代码版权归 PUC-Rio 所有，详见 [Lua License](https://www.lua.org/license.html)。

## 贡献

欢迎提交 Issue 和 Pull Request。请参阅 [贡献指南](CONTRIBUTING.md)。

## 联系方式

- **邮箱**: difierline@yeah.net
- **GitHub**: https://github.com/OxenFxc/LXCLUA-NCORE

## 致谢

- [Lua](https://www.lua.org/) - 原始 Lua 语言
- [stb](https://github.com/nothings/stb) - 单头文件图像处理库
