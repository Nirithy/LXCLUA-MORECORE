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
- **自增/自减**: `++` (e.g. `i++` 后缀自增), `--` (e.g. `i--` 后缀自减)
- **比较**: `!=` (等价于 `~=`), `<=>` (三路比较)
- **类型检查**: `is` (检查对象是否为类的实例)
- **空值处理**: `??` (空值合并), `?.` (可选链)
- **管道操作**: `|>` (正向), `<|` (反向), `|?>` (安全正向)
- **赋值表达式**: `:=` (Walrus Operator) 允许在表达式中进行赋值

```lua
-- 复合赋值与自增
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- 三路比较 (spaceship operator)
-- 返回 -1 (小于), 0 (等于), 1 (大于)
local cmp = 10 <=> 20  -- -1

-- 空值合并 (Null Coalescing)
local val = nil
local res = val ?? "default"  -- "default"

-- 可选链 (Optional Chaining)
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil (不会报错)

-- 管道操作符 (Pipe Operator)
-- x |> f 等价于 f(x) 但返回 x (Tap 语义)
local result = "hello" |> string.upper |> print  -- 打印 HELLO, result 为 "hello"

-- 安全管道 (Safe Pipe)
-- x |?> f 等价于 x and f(x)
local maybe_nil = nil
maybe_nil |?> print  -- (什么都不打印)

-- 赋值表达式 (Walrus Operator)
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. 字符串增强 (Enhanced Strings)

- **插值字符串**: 使用 `"` 或 `'` 包裹，支持 `${var}` 和 `${[expr]}`。
- **原生字符串**: 使用 `_raw` 前缀，不处理转义字符。

```lua
local name = "World"
local age = 25

-- 简单变量插值
print("Hello, ${name}!")  -- Hello, World!

-- 表达式插值
print("Next year: ${[age + 1]}")  -- Next year: 26

-- 原始字符串 (Raw String)
local path = _raw"C:\Windows\System32"  -- C:\Windows\System32
```

### 3. 函数特性 (Function Features)

- **箭头函数**: `(args) => expr` 或 `(args) => { stat }`
- **Lambda 表达式**: `lambda(args) => expr`
- **C 风格定义**: `int add(int a, int b) { ... }`
- **泛型函数**: `function<T>(x) ... end`
- **Async/Await**: `async function`, `await`

```lua
-- 箭头函数 (表达式形式)
local add = (a, b) => a + b
print(add(10, 20))  -- 30

-- 箭头函数 (语句块形式)
local log = (msg) => print("[LOG]: " .. msg)

-- 箭头函数 (-> 语法)
local fast_add = ->(a, b) { return a + b }
local simple_action = -> { print("Action") }

-- Lambda 表达式
local sq = lambda(x): x * x

-- C 风格强类型函数
int sum(int a, int b) {
    return a + b;
}

-- 泛型函数 (带约束)
function(T)(x) requires type(x) == "number"
    return x
end

-- 函数属性 (nodiscard: 忽略返回值时警告)
function<nodiscard> important()
    return "must use this"
end

-- 异步函数
async function fetchData(url)
    local data = await http.get(url)
    return data
end
```

### 4. 面向对象编程 (OOP)

支持完整的类和接口系统：

- **类定义**: `class Name [extends Base] [implements Iface] ... end`
- **成员修饰符**: `public`, `private`, `protected`, `static`, `abstract`, `final`, `sealed`
- **属性访问器**: `get prop() ... end`, `set prop(v) ... end`
- **实例化**: `new Class(...)` (或 `onew`)
- **父类访问**: `super.method(...)` (或 `osuper`)
- **概念 (Concept)**: `concept Name(args) ... end` 定义谓词或类型约束

```lua
interface Drawable
    function draw()
end

class Shape implements Drawable
    function draw()
        print("Drawing shape")
    end
end

class Circle extends Shape
    private _radius = 0

    -- 构造函数
    function __init__(r)
        self._radius = r
    end

    -- Getter 属性
    public get radius()
        return self._radius
    end

    -- Setter 属性
    public set radius(v)
        if v >= 0 then self._radius = v end
    end

    -- 重写方法
    function draw()
        super.draw()  -- 调用父类方法
        print("Drawing circle: " .. self._radius)
    end

    -- 静态方法
    static function create(r)
        return new Circle(r)
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20
c:draw()

-- 概念定义
concept IsPositive(x)
    return x > 0
end
```

### 5. 结构体与类型 (Structs & Types)

- **结构体**: `struct Name { Type field; ... }`
- **超结构体 (SuperStruct)**: `superstruct Name [ key: value, ... ]`
- **泛型结构体**: `struct Box(T) { T value; }`
- **枚举**: `enum Name { A, B=10 }`
- **强类型变量**: `int`, `float`, `bool`, `string`, `double`, `long`, `char` 等
- **解构赋值**: `local take { ... } = expr`

```lua
-- 定义结构体
struct Point {
    int x;
    int y;
}

-- 实例化结构体
local p = Point()
p.x = 10
p.y = 20

-- 定义超结构体 (类似强化版 Table)
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- 定义枚举
enum Color {
    Red,
    Green,
    Blue = 10
}
print(Color.Red)   -- 0
print(Color.Blue)  -- 10

-- 强类型变量声明
int counter = 100;
string message = "Hello";

-- 解构赋值 (Destructuring)
local data = { x = 1, y = 2 }
local take { x, y } = data
print(x, y)  -- 1, 2
```

### 6. 控制流扩展 (Control Flow)

- **Switch**: `switch (exp) { case v: ... }` 或 `switch (exp) do ... end`
- **Try-Catch**: `try ... catch(e) ... finally ... end`
- **Defer**: `defer statement` 或 `defer do ... end`
- **When**: 类似于 `if-elseif-else` 的语法糖
- **Namespace**: `namespace Name { ... }`, `using namespace Name;`
- **Continue**: `continue` 跳过循环当前迭代
- **With**: `with (expr) { ... }` 临时进入对象作用域

```lua
-- Switch 语句
switch (val) do
    case 1:
        print("One")
        break
    case "test":
        print("Test string")
        break
    default:
        print("Other")
end

-- Switch 表达式
local res = switch(val) do
    case 1: return "A"
    case 2: return "B"
end

-- When 语句
local x = 10
when x == 1
    print("x is 1")
case x == 10
    print("x is 10")
else
    print("other")

-- Try-Catch 异常处理
try
    error("Something went wrong")
catch(e)
    print("Caught error: " .. e)
finally
    print("Cleanup resource")
end

-- Defer 延迟执行 (当前作用域结束时触发)
local f = io.open("file.txt", "r")
if f then
    defer f:close() end  -- 自动关闭文件
    -- read file...
end

-- Namespace 命名空间
namespace MyLib {
    int version = 1;
    function test() print("test") end
}
MyLib::test()

using namespace MyLib;
print(version)  -- 1

-- Continue
for i = 1, 10 do
    if i % 2 == 0 then continue end
    print(i)
end

-- With 语句
local obj = { x = 10, y = 20 }
with (obj) {
    print(x + y) -- 30 (直接访问 obj.x 和 obj.y)
}
```

### 7. 元编程 (Metaprogramming)

- **自定义命令 (command)**: 定义 Shell 风格调用的函数
- **自定义关键字 (keyword)**: 定义新的关键字语法
- **自定义运算符 (operator)**: 重载或定义新运算符
- **预处理指令**: `$if`, `$define`, `$include` 等

```lua
-- 自定义命令
command echo(msg)
    print(msg)
end
echo "Hello World"  -- 等价于 echo("Hello World")

-- 自定义运算符 (++ 前缀/后缀)
operator ++ (x)
    return x + 1
end
-- 调用: $$++(val)

-- 预处理指令
$define DEBUG 1
$alias print_debug = print

-- 类型别名与声明
$type MyInt = int
$declare external_var : MyInt <nodiscard>

$if DEBUG
    print_debug("Debug mode on")
$else
    print("Debug mode off")
$end
```

### 8. 内联汇编 (Inline ASM)

支持 `asm` 块直接编写虚拟机指令，用于极致优化或底层操作。

```lua
asm(
    LOADI 0 100
    LOADI 1 200
    ADD 2 0 1
    _print "Result: " 2 ; 编译时打印
    nop 5                ; 插入 5 个 NOP 指令
)
```

### 9. 模块与作用域 (Modules & Scope)

- **导出 (Export)**: `export` 关键字标记模块公开成员。
- **全局 (Global)**: `global` 关键字用于在局部作用域中显式定义全局变量。

```lua
export function myFunc()
    return "exported"
end

export struct MyData { int id; }

-- 局部作用域定义全局函数
global function init()
    print("Global init")
end
```


### 10. 扩展库示例 (Extended Libraries)

#### 文件系统 (fs)
```lua
local fs = require "fs"

-- 检查文件是否存在
if fs.exists("README.md") then
    local info = fs.stat("README.md")
    print("Size: " .. info.size)
end

-- 列出当前目录
local files = fs.ls(".")
for k, v in pairs(files) do
    print(v)
end
```

#### HTTP 网络 (http)
```lua
local http = require "http"

-- HTTP GET 请求
-- local status, body = http.get("http://example.com")

-- TCP Socket 客户端
-- local client = http.client("127.0.0.1", 8080)
-- if client then
--     client:send("ping")
--     client:close()
-- end
```

#### 多线程 (thread)
```lua
local thread = require "thread"

-- 创建并运行线程
local t = thread.create(function(a, b)
    return a + b
end)

-- 等待线程结束并获取结果
local res = t:join(10, 20)
print("Thread result: " .. tostring(res))  -- 30
```


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
