# XCLUA 使用教程

欢迎使用 XCLUA！这是一个基于 Lua 5.4 深度定制的扩展版本，引入了大量现代编程语言特性（如类、强类型、异步编程、宏等）以及强大的系统级库（如指针操作、进程内存读写、文件系统等）。

本教程将详细介绍 XCLUA 的新增语法特性和核心库的使用方法。

## 目录

1. [语法扩展](#语法扩展)
    - [变量与类型](#变量与类型)
    - [函数增强](#函数增强)
    - [面向对象编程](#面向对象编程)
    - [控制流增强](#控制流增强)
    - [结构体与枚举](#结构体与枚举)
    - [命名空间](#命名空间)
    - [操作符扩展](#操作符扩展)
2. [元编程与宏](#元编程与宏)
    - [预处理器](#预处理器)
    - [自定义关键字与命令](#自定义关键字与命令)
    - [内联汇编](#内联汇编)
3. [核心库详解](#核心库详解)
    - [fs (文件系统)](#fs-文件系统)
    - [process (进程操作)](#process-进程操作)
    - [ptr (指针操作)](#ptr-指针操作)
    - [struct (内存结构)](#struct-内存结构)
    - [vm (虚拟机)](#vm-虚拟机)

---

## 语法扩展

### 变量与类型

XCLUA 支持 C++ 风格的变量声明和类型注解（类型注解主要用于静态分析或文档，部分用于运行时检查）。

```lua
-- C++ 风格声明
int x = 10;
float y = 3.14;
string s = "hello";
bool b = true;

-- const 常量
const PI = 3.14159;
-- const function 不能被声明 (会报错)

-- 局部解构 (Take)
local t = { a = 1, b = 2 }
local take { a, b } = t  -- a=1, b=2
```

### 函数增强

#### 箭头函数
更简洁的匿名函数语法，支持 `=>` 和 `->`。

```lua
-- 表达式形式 (自动返回结果)
local add = (a, b) => a + b
local square = x => x * x

-- 语句形式
local print_sum = (a, b) => {
    print(a + b)
}

-- 另一种箭头语法
local f = -> { print("hello") }
```

#### Lambda 表达式
```lua
local f = lambda (x) => x + 1
```

#### 泛型函数
支持类似 C++ 的模板函数定义。

```lua
function<T>(x)
    print("Generic type:", T)
    return x
end
```

#### 异步函数 (Async/Await)
原生支持异步编程模型。

```lua
async function fetchData()
    -- 模拟耗时操作
    return "data"
end

async function main()
    local data = await fetchData() -- 假设 await 是作为一元运算符支持
end
```

### 面向对象编程

XCLUA 内置了完整的类系统。

```lua
-- 定义接口
interface Shape
    function area(self)
end

-- 定义类
class Rectangle implements Shape
    -- 属性
    width = 0
    height = 0

    -- 构造函数 (使用 onew 调用时触发，底层对应 OP_NEWOBJ，通常需要自行实现初始化逻辑)
    function init(self, w, h)
        self.width = w
        self.height = h
    end

    -- 方法
    function area(self)
        return self.width * self.height
    end

    -- Getter/Setter
    get area_val(self)
        return self.width * self.height
    end
end

-- 继承
class Square extends Rectangle
    function init(self, side)
        super.init(self, side, side)
    end
end

-- 实例化
local sq = new Square(10)
print(sq:area())      -- 100
print(sq.area_val)    -- 100
```

支持修饰符：`public` (默认), `private`, `protected`, `static`, `abstract`, `final`, `sealed`.

### 控制流增强

#### Switch 表达式/语句
```lua
local val = 10
local res = switch (val)
    case 10: "ten"
    case 20: "twenty"
    default: "unknown"
end
```

#### Try-Catch-Finally
```lua
try
    error("oops")
catch e
    print("Caught:", e)
finally
    print("Cleanup")
end
```

#### Defer
延迟执行语句块，在当前作用域结束时执行。
```lua
function process()
    local f = io.open("file.txt")
    defer { f:close() }
    -- 处理文件...
end
```

#### 管道操作符
支持函数链式调用。
```lua
-- 前向管道 |>
val |> func1 |> func2  -- 等价于 func2(func1(val))

-- 反向管道 <|
func <| arg  -- 等价于 func(arg)

-- 安全管道 |?> (处理 nil)
nil |?> func -- 返回 nil
```

### 结构体与枚举

#### Struct (C 风格内存结构)
定义紧凑的内存布局，支持与 C 交互。

```lua
struct Point {
    int x = 0,
    int y = 0
}

local p = Point({x=10, y=20})
```

#### Enum
```lua
enum Color {
    Red,
    Green,
    Blue = 10
}
print(Color.Red) -- 0
```

### 命名空间

组织代码，避免全局污染。

```lua
namespace MyLib {
    int version = 1;
    function test() end
}

using namespace MyLib;
print(version);
```

### 操作符扩展

- `+=`, `-=`, `*=`, `/=`, `%=`, `^=`, `..=` : 复合赋值
- `++` : 自增 (后置)
- `?.` : 安全导航 (例如 `obj?.prop`，如果 obj 为 nil 则返回 nil)
- `??` : 空值合并 (例如 `a ?? b`，如果 a 不为 nil 返回 a，否则返回 b)
- `${var}` : 字符串插值 (例如 `"Value: ${x}"`)
- `[ -f "file" ]` : Shell 风格条件测试 (检测文件存在等)

## 元编程与宏

### 预处理器
支持编译期指令，以 `$` 开头。

```lua
$define DEBUG = true
$if DEBUG
    print("Debug mode")
$else
    print("Release mode")
$end

$include "header.lua"
```

### 自定义关键字与命令

可以扩展语言本身的语法。

```lua
-- 定义新关键字
keyword unless(cond)
    if not cond then
        local _ = ((function()
```
*(注：宏系统较为复杂，通常涉及闭包和代码生成)*

**命令语法**：允许像 Shell 一样调用函数。
```lua
command echo(msg)
    print(msg)
end

echo "Hello World" -- 等价于 echo("Hello World")
```

### 内联汇编
直接编写 XCLUA 虚拟机的字节码。

```lua
asm {
    loadk 0, "Hello ASM";
    getglobal 1, "print";
    call 1, 2, 1;
}
```

## 核心库详解

### fs (文件系统)

提供跨平台的文件系统操作。

- `fs.ls(path)`: 返回目录下的文件列表（表）。
- `fs.isdir(path)`: 判断是否为目录。
- `fs.isfile(path)`: 判断是否为文件。
- `fs.mkdir(path)`: 创建目录。
- `fs.rm(path)`: 删除文件或空目录。
- `fs.exists(path)`: 检查路径是否存在。
- `fs.stat(path)`: 获取文件信息（表：`size`, `mtime`, `ctime`, `mode`, `isdir`, `isfile`）。
- `fs.currentdir()`: 获取当前工作目录。
- `fs.chdir(path)`: 切换工作目录。
- `fs.abs(path)`: 获取绝对路径。
- `fs.basename(path)`: 获取文件名。
- `fs.dirname(path)`: 获取目录名。
- `fs.set_permissions({root="path", read_only=bool})`: 设置沙箱权限。

### process (进程操作)

仅 Linux 平台可用，用于进程内存读写。

- `process.open(pid)`: 打开进程，返回进程句柄。
- `process.getpid()`: 获取当前进程 ID。
- **句柄方法**:
    - `p:read(addr, size)`: 从指定地址读取 `size` 字节。
    - `p:write(addr, data)`: 向指定地址写入数据。
    - `p:close()`: 关闭句柄。

### ptr (指针操作)

提供类似 C 语言的指针操作能力。

- `ptr.new(addr)` / `ptr(addr)`: 从地址创建指针。
- `ptr.malloc(size)`: 分配内存，返回指针。
- `ptr.free(p)`: 释放内存。
- `ptr.addr(p)`: 获取指针的地址数值。
- `ptr.read(p, type)`: 读取值。`type`: "int", "float", "byte", "string", "size_t" 等。
- `ptr.write(p, type, val)`: 写入值。
- `ptr.string(p, [len])`: 读取 C 字符串。
- `ptr.copy(dst, src, len)`: 内存拷贝 (memcpy)。
- `ptr.move(dst, src, len)`: 内存移动 (memmove)。
- `ptr.fill(p, val, len)`: 内存填充 (memset)。
- `ptr.compare(p1, p2, len)`: 内存比较 (memcmp)。
- `ptr.tohex(p, len)`: 转换为十六进制字符串。
- **运算**: 指针支持 `+`, `-` 运算（偏移）和 `tostring`。

### struct (内存结构)

配合 `struct` 语法使用，用于定义 C 兼容的内存布局。

- `struct.define(name, fields)`: 定义结构体。
- `array(type)[size]`: 创建指定类型的数组（可以是 "int", "float" 等基础类型或结构体定义）。
- **Struct 实例**: 访问字段会自动读写底层内存。
- **Array 实例**: 支持索引访问 `arr[1]`。

### vm (虚拟机)

提供对 Lua 虚拟机的底层控制和内省。

- `vm.execute(func)`: 执行函数。
- `vm.gcstep(step)`: 执行 GC 步进。
- `vm.memory()`: 返回内存使用量。
- `vm.getstack()`: 获取当前栈信息。
- `vm.getci()`: 获取当前调用信息。
- `vm.getregistry()`: 获取注册表。
- `vm.getglobalenv()`: 获取全局环境。
- `vm.setglobalenv(table)`: 设置全局环境。
- `vm.dump(func)`: 导出函数字节码（类似 `string.dump`）。
- 包含大量标准 Lua API 的封装（如 `vm.tonumber`, `vm.rawget` 等）。
