# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

基于 **Lua 5.5 (Custom)** 深度定制的高性能嵌入式脚本引擎，提供增强的安全特性、扩展库支持和优化的字节码编译。

[English Documentation](README.md)

---

## 特性 (Features)

### 核心增强 (Core Enhancements)

- **安全编译 (Secure Compilation)**: 动态 OPcode 映射、时间戳加密、SHA-256 完整性验证。
- **自定义 VM (Custom VM)**: 实现 XCLUA 指令集与解释器，优化分发性能。
- **语法扩展 (Syntax Extensions)**: 引入类 (Class)、Switch、Try-Catch、箭头函数、管道操作符等现代语言特性。

### 扩展模块 (Extension Modules)

| 模块 (Module) | 描述 (Description) |
|--------|------------------------|
| `json` | 内置 JSON 解析与序列化 |
| `lclass` | 面向对象编程支持 (类、继承、接口) |
| `lbitlib` | 位运算库 |
| `lboolib` | 布尔类型增强 |
| `ludatalib` | 二进制数据序列化 |
| `lsmgrlib` | 内存管理工具 |
| `process` | 进程管理 (仅限 Linux) |
| `http` | HTTP 客户端/服务端与 Socket |
| `thread` | 多线程支持 |
| `fs` | 文件系统操作 |
| `struct` | C 风格结构体与强类型数组 |

---

## 语法扩展 (Syntax Extensions)

LXCLUA-NCore 引入了大量现代语言特性，极大地扩展了 Lua 5.5 的能力。

### 1. 扩展运算符 (Extended Operators)

支持复合赋值、自增/自减、三路比较 (Spaceship)、空值合并、可选链、管道操作符以及 Walrus 赋值操作符。

```lua
-- 复合赋值与自增
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- 三路比较 (-1, 0, 1)
local cmp = 10 <=> 20  -- -1

-- 空值合并
local val = nil
local res = val ?? "default"  -- "default"

-- 可选链
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil

-- 管道操作符
local result = "hello" |> string.upper  -- "HELLO"

-- 安全管道
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (什么都不做)

-- Walrus 赋值表达式
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. 字符串增强 (Enhanced Strings)

- **插值 (Interpolation)**: 字符串中使用 `${var}` 或 `${[expr]}`。
- **原生字符串 (Raw Strings)**: 使用 `_raw` 前缀，忽略转义序列。

```lua
local name = "World"
print("Hello, ${name}!")  -- Hello, World!

local path = _raw"C:\Windows\System32"
```

### 3. 函数特性 (Function Features)

支持箭头函数、Lambda 表达式、C 风格定义、泛型函数和 Async/Await。

```lua
-- 箭头函数
local add = (a, b) => a + b
local log = ->(msg) { print("[LOG]: " .. msg) }

-- Lambda 表达式
local sq = lambda(x): x * x

-- C 风格函数
int sum(int a, int b) {
    return a + b;
}

-- 泛型函数
local function Factory(T)(val)
    return { type = T, value = val }
end
local obj = Factory("int")(99)

-- 异步函数 (Async/Await)
async function fetchData(url)
    local data = await http.get(url)
    return data
end
```

### 4. 面向对象编程 (OOP)

完整的类和接口系统，支持访问修饰符 (`private`, `public`, `static`, `final`, `abstract`, `sealed`) 和属性 (`get`/`set`)。

```lua
interface Drawable
    function draw(self)
end

class Shape implements Drawable
    function draw(self)
        -- 类似抽象方法
    end
end

class Circle extends Shape
    private _radius = 0

    function __init__(self, r)
        self._radius = r
    end

    -- 属性 Getter/Setter
    get radius(self)
        return self._radius
    end

    set radius(self, v)
        if v >= 0 then self._radius = v end
    end

    function draw(self)
        super.draw(self)
        return "Drawing circle: " .. self._radius
    end

    static function create(r)
        return new Circle(r)
    end
end

local c = Circle.create(10)
c.radius = 20
print(c.radius)  -- 20
```

### 5. 结构体与类型 (Structs & Types)

```lua
-- 结构体
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- 超结构体 (SuperStruct - 增强表定义)
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- 枚举
enum Color {
    Red,
    Green,
    Blue = 10
}

-- 解构赋值
local data = { x = 1, y = 2 }
local take { x, y } = data
```

### 6. 控制流 (Control Flow)

```lua
-- Switch 语句
switch (val) do
    case 1:
        print("One")
        break
    default:
        print("Other")
end

-- When 语句 (模式匹配风格)
do
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end

-- Try-Catch-Finally 异常处理
try
    error("Error")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- Defer 延迟执行
defer do print("Executes at scope exit") end

-- 命名空间 (Namespace) 与 Using
namespace MyLib {
    function test() return "test" end
}
using namespace MyLib;
```

### 7. 元编程与宏 (Metaprogramming & Macros)

```lua
-- 自定义命令
command echo(msg)
    print(msg)
end
echo "Hello World"

-- 自定义运算符
operator ++ (x)
    return x + 1
end
-- 使用 $$ 前缀调用
local res = $$++(10)

-- 预处理指令
$define DEBUG 1
$if DEBUG
    print("Debug mode")
$end

-- 对象宏
local x = 10
local obj = $object(x) -- {x=10}
```

### 8. 内联汇编 (Inline ASM)

直接编写虚拟机指令。请使用 `newreg` 安全地分配寄存器。

```lua
asm(
    newreg r0
    LOADI r0 100
    -- ... instructions
)
```

---

## 编译与测试 (Build & Test)

### 编译 (Build)

```bash
# Linux
make linux

# Windows (MinGW)
make mingw
```

### 验证 (Verification)

运行测试套件以验证所有特性：

```bash
./lxclua tests/verify_docs_full.lua
./lxclua tests/test_parser_features.lua
```

## 许可证 (License)

[MIT License](LICENSE).
Lua original code Copyright © PUC-Rio.
