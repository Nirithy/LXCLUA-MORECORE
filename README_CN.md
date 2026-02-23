# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

基于 **Lua 5.5 (Custom)** 的高性能嵌入式脚本引擎，增强了安全性、扩展库和优化的字节码编译。

[English Documentation](README.md)

---

## 功能特性

### 核心增强

- **安全编译**：动态操作码映射、时间戳加密、SHA-256 完整性校验。
- **自定义 VM**：实现 XCLUA 指令集，优化调度。
- **语法扩展**：包含类、Switch、Try-Catch、箭头函数、管道操作符等现代语言特性。
- **Shell 风格条件测试**：内置支持 Shell 风格的测试表达式（例如 `[ -f "file.txt" ]`）。

### 扩展模块

| 模块 | 描述 |
|--------|------------------------|
| `json` | 内置 JSON 解析/序列化 |
| `lclass` | OOP 支持（类、继承、接口） |
| `lbitlib` | 位运算 |
| `lboolib` | 布尔增强 |
| `ludatalib` | 二进制数据序列化 |
| `lsmgrlib` | 内存管理工具 |
| `process` | 进程管理 (仅限 Linux) |
| `http` | HTTP 客户端/服务端 & Socket |
| `thread` | 多线程支持 |
| `fs` | 文件系统操作 |
| `struct` | C 风格结构体 & 数组 |

---

## 语法扩展

LXCLUA-NCore 引入了现代语言特性以扩展 Lua 5.5。

### 1. 扩展操作符

支持复合赋值、自增/自减、太空船操作符、空值合并、可选链、管道操作符和海象操作符。

```lua
-- 复合赋值与自增
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- 太空船操作符 (-1, 0, 1)
local cmp = 10 <=> 20  -- -1

-- 空值合并
local val = nil
local res = val ?? "default"  -- "default"

-- 可选链
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil

-- 管道操作符
local function double(x) return x * 2 end
local result = 10 |> double  -- 20

-- 安全管道 (如果为 nil 则跳过)
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (不执行)

-- 海象操作符 (赋值表达式)
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. 增强字符串

- **插值**：字符串内使用 `${var}` 或 `${[expr]}`。
- **原生字符串**：前缀为 `_raw`，忽略转义序列。

```lua
local name = "World"
print("Hello, ${name}!")  -- Hello, World!

local calc = "1 + 1 = ${[1+1]}"  -- 1 + 1 = 2

local path = _raw"C:\Windows\System32"
```

### 3. 函数特性

支持箭头函数、Lambda、C 风格定义、泛型和 async/await。

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

-- Async/Await
async function fetchData(url)
    -- local data = await http.get(url) -- (需要异步运行时)
    return "data"
end
```

### 4. 面向对象编程 (OOP)

完整的类和接口系统，支持修饰符（`private`、`public`、`static`、`final`、`abstract`、`sealed`）和属性（`get`/`set`）。

```lua
interface Drawable
    function draw(self)
end

class Shape implements Drawable
    function draw(self)
        -- 类似抽象方法的行为
    end
end

-- 密封类 (不可被继承)
sealed class Circle extends Shape
    private _radius = 0

    function __init__(self, r)
        self._radius = r
    end

    -- 带 Getter/Setter 的属性
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

### 5. 结构体与类型

```lua
-- 结构体
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- 概念 (类型谓词)
concept IsPositive(x)
    return x > 0
end
-- 或单表达式形式
concept IsEven(x) = x % 2 == 0

-- SuperStruct (增强表定义)
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

### 6. 控制流

```lua
-- Switch 语句
switch (val) do
    case 1:
        print("One")
        break
    default:
        print("Other")
end

-- When 语句 (模式匹配)
do
    when x == 1
        print("x is 1")
    case x == 10
        print("x is 10")
    else
        print("other")
end

-- Try-Catch-Finally
try
    error("Error")
catch(e)
    print("Caught: " .. e)
finally
    print("Cleanup")
end

-- Defer
defer do print("Executes at scope exit") end

-- With 语句
local ctx = { val = 10 }
with (ctx) {
    print(val) -- 10
}

-- 命名空间 & Using
namespace MyLib {
    function test() return "test" end
}
using namespace MyLib; -- 导入所有
-- using MyLib::test;  -- 导入特定成员
```

### 7. Shell 风格测试

内置使用 `[ ... ]` 语法的条件测试。

```lua
if [ -f "config.lua" ] then
    print("Config file exists")
end

if [ "a" == "a" ] then
    print("Strings match")
end

if [ 10 -gt 5 ] then
    print("10 > 5")
end
```

### 8. 元编程 & 宏

```lua
-- 自定义命令
command echo(msg)
    print(msg)
end
echo "Hello World"

-- 自定义操作符
operator ++ (x)
    return x + 1
end
-- 使用 $$ 前缀调用
local res = $$++(10)

-- 预处理器指令
$define DEBUG 1
$alias CONST_VAL = 100
$type MyInt = int

$if DEBUG
    print("Debug mode")
$else
    print("Release mode")
$end

$declare g_var: MyInt

-- 对象宏
local x = 10
local obj = $object(x) -- {x=10}
```

### 9. 内联汇编 (Inline ASM)

直接编写 VM 指令。使用 `newreg` 安全分配寄存器。
支持伪指令如 `rep`, `_if`, `_print`。

```lua
asm(
    newreg r0
    LOADI r0 100

    -- 编译时循环
    rep 5 {
        ADDI r0 r0 1
    }

    -- 条件汇编
    _if 1
       _print "Compiling this block"
    _endif

    -- 嵌入数据
    -- db 1, 2, 3, 4
    -- str "RawData"

    RETURN1 r0
)
```

---

## 构建与测试

### 构建

```bash
# Linux
make linux

# Windows (MinGW)
make mingw
```

### 验证

运行测试套件以验证所有功能：

```bash
./lxclua tests/verify_docs_full.lua
./lxclua tests/test_parser_features.lua
./lxclua tests/test_advanced_parser.lua
```

## 许可证

[MIT License](LICENSE).
Lua original code Copyright © PUC-Rio.
