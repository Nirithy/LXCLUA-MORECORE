# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

基于 **Lua 5.5 (Custom)** 深度定制的高性能嵌入式脚本引擎，提供增强的安全特性、扩展库支持和优化的字节码编译。

---

## Tested Platforms / 测试平台

| Platform | Status | Bytecode Interop |
|----------|--------|------------------|
| Windows (MinGW) | ✅ Passed | ✅ |
| Arch Linux | ✅ Passed | ✅ |
| Ubuntu | ✅ Passed | ✅ |
| Android (Termux) | ✅ Passed | ✅ |
| Android (LXCLUA JNI) | ✅ Passed | ✅ |

## Features / 特性

### Core Enhancements / 核心增强

- **Secure Compilation / 安全编译**: Dynamic OPcode mapping, timestamp encryption, SHA-256 integrity verification. (动态 OPcode 映射、时间戳加密、SHA-256 完整性验证)
- **PNG Encapsulation / PNG 封装**: Compiled bytecode embedded in PNG image format for obfuscation. (编译后的字节码嵌入 PNG 图像格式，增强混淆效果)
- **Anti-Reverse / 防逆向保护**: Multi-layer encryption mechanisms. (多层加密机制有效防止字节码被反编译或篡改)
- **Virtualization / 虚拟化保护**: `vmprotect` library supports virtualizing functions to hide logic. (提供 `vmprotect` 库，支持将函数转换为虚拟化代码执行)
- **Custom VM / 自定义 VM**: Implements XCLUA instruction set. (实现 XCLUA 指令集与解释器)

### Extension Modules / 扩展模块

| Module | Description (功能描述) |
|--------|------------------------|
| `json` | Built-in JSON parsing/serialization (内置 JSON 解析与序列化) |
| `sha256` | SHA-256 hash computation (SHA-256 哈希计算) |
| `lclass` | OOP support (classes, inheritance) (面向对象编程支持) |
| `lbitlib` | Bitwise operations (位运算库) |
| `lboolib` | Boolean enhancements (布尔类型增强) |
| `ludatalib` | Binary data serialization (二进制数据序列化) |
| `lsmgrlib` | Memory management utilities (内存管理工具) |
| `ltranslator`| Code translator (代码翻译器) |
| `logtable` | Log table support (日志表支持) |
| `lptrlib` | Pointer operations (指针操作库) |
| `lvmlib` | VM extension interface (虚拟机扩展接口) |
| `process` | Process management (Linux only) (进程管理与内存读写) |
| `http` | HTTP client/server & Socket (HTTP 客户端/服务端与 Socket) |
| `vmprotect`| Bytecode virtualization (Lua 字节码虚拟化保护) |
| `thread` | Multithreading support (多线程支持) |
| `fs` | File system operations (文件系统操作) |
| `struct` | C-style structs & arrays (C 风格结构体与强类型数组) |

---

## Syntax Extensions / 语法扩展

LXCLUA-NCore introduces modern language features to extend Lua 5.5.
LXCLUA-NCore 引入了大量现代语言特性，极大地扩展了 Lua 5.5 的能力。

### 1. Extended Operators / 扩展运算符

Supports compound assignments, increment/decrement, spaceship operator, null coalescing, optional chaining, pipe operators, and walrus operator.
支持复合赋值、自增/自减、三路比较、空值合并、可选链、管道操作符以及 Walrus 赋值操作符。

```lua
-- Compound Assignment & Increment / 复合赋值与自增
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- Spaceship Operator / 三路比较
-- Returns -1 (less), 0 (equal), 1 (greater)
local cmp = 10 <=> 20  -- -1

-- Null Coalescing / 空值合并
local val = nil
local res = val ?? "default"  -- "default"

-- Optional Chaining / 可选链
do
    local config = { server = { port = 8080 } }
    local port = config?.server?.port  -- 8080
    local timeout = config?.client?.timeout  -- nil
end

-- Pipe Operator / 管道操作符
-- x |> f is equivalent to f(x)
local result = "hello" |> string.upper  -- "HELLO"

-- Safe Pipe / 安全管道
-- x |?> f is equivalent to x and f(x)
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (does nothing / 什么都不做)

-- Walrus Operator / 赋值表达式
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. Enhanced Strings / 字符串增强

- **Interpolation / 插值**: `${var}` inside strings.
- **Raw Strings / 原生字符串**: Prefixed with `_raw`, ignores escape sequences.

```lua
local name = "World"
local age = 25

-- Interpolation / 插值
print("Hello, ${name}!")  -- Hello, World!

-- Raw String / 原始字符串
local path = _raw"C:\Windows\System32"  -- C:\Windows\System32
```

### 3. Function Features / 函数特性

Supports arrow functions, lambdas, C-style definitions, generics, and async/await.
支持箭头函数、Lambda 表达式、C 风格定义、泛型函数和 Async/Await。

```lua
-- Arrow Function (Expression) / 箭头函数 (表达式)
local add = (a, b) => a + b
print(add(10, 20))  -- 30

-- Arrow Function (Block) / 箭头函数 (代码块)
-- Note: -> must use { } for block / 必须使用 { }
local log = ->(msg) { print("[LOG]: " .. msg) }
log("test")

-- Lambda Expression / Lambda 表达式
local sq = lambda(x): x * x
print(sq(10)) -- 100

-- C-style Function / C 风格函数
int sum(int a, int b) {
    return a + b;
}

-- Generic Function / 泛型函数
-- 'requires' checks type parameter T / requires 检查类型参数 T
local f = function(T)(x) requires type(T) == "number"
    return x
end
local inner = f(10) -- T=10 (pass)
print(inner("hello"))

-- Function Attributes / 函数属性
function important() <nodiscard>
    return "must use this"
end

-- Async/Await / 异步函数
async function fetchData(url)
    local data = await http.get(url)
    return data
end
```

### 4. Object-Oriented Programming (OOP) / 面向对象编程

Complete class and interface system.
完整的类和接口系统。

```lua
interface Drawable
    function draw(self)
end

class Shape implements Drawable
    function draw(self)
        -- abstract-like behavior
    end
end

class Circle extends Shape
    private _radius = 0

    -- Constructor / 构造函数
    function __init__(self, r)
        self._radius = r
    end

    -- Getter
    public get radius(self)
        return self._radius
    end

    -- Setter (binds 'self' automatically / 自动绑定 self)
    public set radius(self, v)
        if v >= 0 then self._radius = v end
    end

    -- Override / 重写
    function draw(self)
        super.draw(self)
        print("Drawing circle: " .. self._radius)
    end

    -- Static Method / 静态方法
    static function create(r)
        return new Circle(r)
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20
c:draw()

-- Concept / 概念定义
concept IsPositive(x)
    return x > 0
end
```

### 5. Structs & Types / 结构体与类型

```lua
-- Struct / 结构体
struct Point {
    int x;
    int y;
}

local p = Point()
p.x = 10; p.y = 20

-- SuperStruct (Enhanced Table) / 超结构体
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- Enum / 枚举
enum Color {
    Red,
    Green,
    Blue = 10
}

-- Strong Typed Variables / 强类型变量
int counter = 100;
string message = "Hello";

-- Destructuring / 解构赋值
local data = { x = 1, y = 2 }
local take { x, y } = data
```

### 6. Slice Syntax / 切片语法

Python-style table slicing.
Python 风格的表切片操作。

```lua
local t = {10, 20, 30, 40, 50}

-- [start:end] (inclusive / 包含 end)
local sub1 = t[2:4]  -- {20, 30, 40}

-- Omit start (defaults to 1) / 省略 start
local sub2 = t[:3]   -- {10, 20, 30}

-- Step / 步长
local sub3 = t[1:5:2] -- {10, 30, 50}
```

### 7. Control Flow / 控制流

```lua
-- Switch Statement / Switch 语句
local val = 1
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

-- Switch Expression / Switch 表达式
local res = switch(val) do
    case 1: return "A"
    case 2: return "B"
end

-- When (Pattern Matching-like) / When 语句
do
    local x = 10
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end

-- Try-Catch / 异常处理
try
    error("Error")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- Defer / 延迟执行
local f = { close = function() print("closed") end }
if f then
    defer do f:close() end
    print("reading...")
end

-- Namespace / 命名空间
namespace MyLib {
    int version = 1;
    function test() print("test") end
}
using namespace MyLib;

-- Continue / 循环继续
for i = 1, 10 do
    if i % 2 == 0 then continue end
    print(i)
end

-- With Statement / With 语句
local obj = { x = 10, y = 20 }
with (obj) {
    print(x + y) -- 30
}
```

### 8. Metaprogramming / 元编程

```lua
-- Initialize tables / 初始化元表
if not _G._OPERATORS then _G._OPERATORS = {} end
if not _G._CMDS then _G._CMDS = {} end

-- Custom Command / 自定义命令
command echo(msg)
    print(msg)
end
echo "Hello World"

-- Custom Operator / 自定义运算符
operator ++ (x)
    return x + 1
end
-- Call: $$++(val)

-- Preprocessor / 预处理指令
$define DEBUG 1

$if DEBUG
    print("Debug mode on")
$else
    print("Debug mode off")
$end
```

### 9. Inline ASM / 内联汇编

Write VM instructions directly.
直接编写虚拟机指令。

```lua
asm(
    newreg r0
    newreg r1
    newreg r2
    LOADI r0 100
    LOADI r1 200
    ADD r2 r0 r1
    _print "Result: " r2
)
```

### 10. Modules / 模块

```lua
export function myFunc()
    return "exported"
end

export struct MyData { int id; }

global function init()
    print("Global init")
end
```

### 11. Advanced Features / 高级特性

```lua
-- Object Macro / 对象宏
local x, y = 10, "hello"
local obj = $object(x, y) -- {x=10, y="hello"}

-- Operator Call / 运算符调用
local res = $$+(10, 20)

-- Lambda Shorthand / Lambda 简写
local dbl = lambda(x): x * 2

-- Concept Expression / 概念表达式
concept IsPositive(x) = x > 0
```

### 12. Libraries / 扩展库

```lua
local fs = require "fs"
local http = require "http"
local thread = require "thread"

-- FS
if fs.exists("README.md") then
    local info = fs.stat("README.md")
end

-- Thread
local t = thread.create(function(a, b)
    return a + b
end, 10, 20)
print(t:join())
```

---

## Quick Start / 快速开始

### Build / 编译

```bash
# Windows (MinGW)
make mingw

# Linux
make linux

# Android (Termux)
make termux
```

### Verify / 验证

```bash
make test
```

### Usage / 使用

Run a script:
运行脚本：

```bash
./lxclua script.lua
```

Compile to bytecode:
编译为字节码：

```bash
./luac -o output.luac script.lua
```

---

## License / 许可证

[MIT License](LICENSE).
Lua original code Copyright © PUC-Rio.

## Contact / 联系方式

- **Email**: difierline@yeah.net
- **GitHub**: https://github.com/OxenFxc/LXCLUA-NCORE
