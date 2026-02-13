# LXCLUA-NCore 教程

LXCLUA-NCore 是一个基于 Lua 5.5 (Custom) 深度定制的高性能嵌入式脚本引擎。它在保留 Lua 简洁性的基础上，引入了现代编程语言的诸多特性，如强类型声明、面向对象编程、异步编程、元编程以及强大的内置库。

本教程将详细介绍 LXCLUA 的新增语法特性和核心库的使用方法。

## 目录

1.  [基础语法扩展](#1-基础语法扩展)
2.  [流程控制](#2-流程控制)
3.  [函数增强](#3-函数增强)
4.  [面向对象编程](#4-面向对象编程)
5.  [高级特性](#5-高级特性)
6.  [扩展库](#6-扩展库)

---

## 1. 基础语法扩展

### 类型声明 (C++ 风格)
LXCLUA 支持类似 C++/Java 的变量类型声明语法。虽然 Lua 是动态类型语言，但这种语法糖有助于代码的可读性和静态分析。

```lua
int x = 10;
float pi = 3.14;
string name = "Lua";
bool isActive = true;

-- 函数参数类型提示
int function add(int a, int b)
    return a + b
end
```

### 复合赋值与自增
支持常见的复合赋值运算符和自增运算符。

```lua
local x = 10
x += 5      -- x = x + 5
x++         -- x = x + 1
x -= 2      -- x = x - 2
x *= 3      -- x = x * 3
x /= 2      -- x = x / 2
```

### 注释
保留 Lua 标准注释 `--`，同时支持 C 风格注释（如果启用了对应配置，但在标准模式下推荐使用 Lua 风格）。

---

## 2. 流程控制

### Switch 语句
原生支持 `switch-case` 结构，结构清晰，无需使用 `if-elseif` 链。同时支持作为表达式使用。

```lua
local val = 2

-- 语句形式
switch (val)
    case 1:
        print("One")
        break
    case 2:
        print("Two")
        break
    default:
        print("Other")
end

-- 表达式形式 (类似 Rust 的 match)
local res = switch (val)
    case 1: 100
    case 2: 200
    default: 0
end
```

### Try-Catch-Finally
内置异常处理机制。

```lua
try
    error("Something wrong")
catch (e)
    print("Caught error: " .. e)
finally
    print("Cleanup")
end
```

### Defer
类似 Go 语言的 `defer`，确保代码块在当前作用域结束时执行（常用于资源释放）。

```lua
local f = io.open("file.txt", "r")
if f then
    defer f:close() -- 作用域结束时自动关闭文件
    -- 读取文件...
end
```

### If 表达式
支持三元运算符风格的 `if` 表达式。

```lua
local max = if a > b then a else b
```

### 管道运算符
支持函数链式调用，提高代码可读性。

```lua
-- |> 正向管道: x |> f 等价于 f(x)
local result = "hello" |> string.upper |> string.reverse
-- 结果: "OLLEH"

-- <| 反向管道: f <| x 等价于 f(x)
local val = math.sqrt <| 16 -- 4.0

-- |?> 安全管道: x |?> f 如果 x 为 nil 则返回 nil，否则返回 f(x)
local safe = nil |?> string.upper -- nil
```

---

## 3. 函数增强

### 箭头函数
更简洁的函数定义方式。

```lua
-- 表达式形式 (自动返回结果)
local add = =>(a, b) { a + b }
-- 或者更简短:
local square = =>(x) x * x

-- 语句形式 (需要显式 return，除非是单行)
local process = ->(x) {
    print("Processing " .. x)
}
```

### Lambda
`lambda` 关键字用于定义匿名函数。

```lua
local f = lambda(x, y) => x + y
```

### Async/Await
原生支持异步编程模型。

```lua
async function fetchData()
    local data = await http.get("https://example.com")
    return data
end
```

---

## 4. 面向对象编程

LXCLUA 引入了完整的类系统，支持继承、接口、修饰符等特性。

### 类定义 (Class)

```lua
class Animal
    public string name = "Unknown"

    function init(name) -- 构造函数（部分实现可能使用 new）
        self.name = name
    end

    public function speak()
        print(self.name .. " makes a sound")
    end
end

-- 继承
class Dog extends Animal
    public function speak()
        print(self.name .. " barks")
    end
end

local d = new Dog("Buddy")
d:speak() -- "Buddy barks"
```

### 接口 (Interface)

```lua
interface Printable
    function print()
end

class Document implements Printable
    function print()
        print("Printing document...")
    end
end
```

### 结构体 (Struct)
C 风格的结构体定义，支持类型检查。

```lua
struct Point {
    int x;
    int y;
}

local p = Point{x=10, y=20}
```

### 枚举 (Enum)

```lua
enum Color {
    Red = 1,
    Green,
    Blue
}
print(Color.Green) -- 2
```

---

## 5. 高级特性

### 命名空间 (Namespace)
用于组织代码，避免全局命名冲突。

```lua
namespace MyLib {
    int version = 1
    function test() print("Test") end
}

MyLib::test()

using namespace MyLib;
test() -- 直接调用
```

### 字符串插值
支持在字符串中直接嵌入变量或表达式。

```lua
local name = "World"
print("Hello, ${name}!")
print("Result: ${[1 + 2]}") -- 支持表达式
```

### 预处理器
支持类似 C 的预处理指令，在编译期执行。

```lua
$define DEBUG = true

$if DEBUG $then
    print("Debug mode on")
$else
    print("Debug mode off")
$end
```

### 属性 (Attributes)
支持元数据注解，如 `<nodiscard>`。

```lua
declare <nodiscard> function essential() ... end
```

### 内联汇编 (ASM)
允许在 Lua 代码中直接嵌入 VM 指令（仅供高级用户使用）。

```lua
asm {
    LOADI R0 100;
    RETURN1 R0;
}
```

---

## 6. 扩展库

### fs (文件系统)
提供文件和目录操作。
- `fs.ls(path)`: 列出目录
- `fs.exists(path)`: 检查存在
- `fs.mkdir(path)`: 创建目录
- `fs.read(path)`, `fs.write(path, content)`

### process (进程管理)
Linux 平台下支持进程操作和内存读写。
- `process.getpid()`
- `process.open(pid)`
- `proc:read(addr, size)`
- `proc:write(addr, data)`

### ptr (指针操作)
强大的指针操作库，支持直接内存访问。
- `ptr.malloc(size)`
- `ptr.read(p, type)`, `ptr.write(p, type, val)`
- 支持指针算术运算 `p + 1`

### 其他库
- **http**: HTTP 客户端/服务端支持。
- **json**: JSON 编解码。
- **vm**: 虚拟机控制接口。
- **thread**: 多线程支持。

---

*注意：LXCLUA 仍在活跃开发中，部分语法特性可能会随版本更新而调整。*
