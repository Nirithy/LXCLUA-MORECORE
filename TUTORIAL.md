# LXCLUA-NCore 教程

## 1. 简介
LXCLUA-NCore 是基于 Lua 5.5 的深度定制版本，提供了大量现代化编程特性，包括面向对象编程、强类型支持、异步/await、模式匹配、宏系统以及内联汇编等。本文档将全面介绍这些扩展关键字及其使用方法。

## 2. 基础语法扩展

### 注释
支持 C 风格的多行注释和单行注释：
```lua
// 这是单行注释
/* 这是
   多行注释 */
```

### 复合赋值运算符
支持类似于 C/C++ 的复合赋值操作：
```lua
local x = 10
x += 5      -- x = x + 5
x -= 2      -- x = x - 2
x *= 3      -- x = x * 3
x /= 2      -- x = x / 2
x //= 2     -- x = x // 2 (整除)
x %= 3      -- x = x % 3
x++         -- 自增 x = x + 1
```

### 字符串插值
使用 `${...}` 在字符串中嵌入变量或表达式：
```lua
local name = "World"
local age = 25
print("Hello, ${name}!")           -- Hello, World!
print("Next year: ${[age + 1]}")   -- Next year: 26
print("Price: $$100")              -- Price: $100 (转义)
```
支持 `_raw` 前缀创建原始字符串（不转义）：
```lua
local path = _raw"C:\Windows\System32"
```

### 条件表达式
```lua
local max = if a > b then a else b end
```

### Switch 表达式
`switch` 既可以作为语句，也可以作为表达式返回值：
```lua
local type_name = switch (val)
    case 1: "Integer"
    case "s": "String"
    default: "Unknown"
end
```

### When 语句
类似于 `if-elseif-else` 的语法糖：
```lua
when x > 0 then
    print("Positive")
case x < 0 then
    print("Negative")
else
    print("Zero")
end
```

## 3. 变量与类型系统

### 强类型声明
支持 C 风格的变量声明：
```lua
int x = 10;
float y = 3.14;
bool flag = true;
string s = "text";
```
也支持 `declare` 关键字声明全局类型提示：
```lua
$declare MyGlobal: int
```

### 解构赋值 (take)
使用 `take` 关键字解构表或数组：
```lua
local data = { x = 10, y = 20, z = 30 }
take { x, y } = data
print(x, y) -- 10, 20

local list = { 1, 2, 3 }
take { a, , c } = list -- 跳过第二个元素
print(a, c) -- 1, 3
```

### Defer 语句
`defer` 用于在当前作用域结束时执行代码（类似 Go）：
```lua
local f = io.open("file.txt")
defer f:close() end
-- 文件会在当前块结束时自动关闭
```

### Const 常量
声明不可修改的变量：
```lua
const PI = 3.14159
-- PI = 3 -- 错误：无法给常量赋值
```

## 4. 数据结构扩展

### Struct 结构体
定义具有特定字段的结构体：
```lua
struct Point {
    x = 0,
    y = 0
}

local p = Point{ x = 10, y = 20 }
print(p.x)
```
支持带类型的字段：
```lua
struct Person {
    string name,
    int age = 18
}
```

### Enum 枚举
定义枚举类型：
```lua
enum Color {
    Red,
    Green,
    Blue = 10
}
print(Color.Red)   -- 0
print(Color.Blue)  -- 10
```

### 数组 (Typed Arrays)
支持创建指定类型的数组：
```lua
int arr[10] -- 创建包含10个整数的数组
```

## 5. 函数扩展

### 箭头函数
简洁的函数定义语法：
```lua
local add = (a, b) => a + b      -- 自动返回表达式
local log = (msg) -> { print(msg) } -- 语句块
```

### Lambda 表达式
```lua
local f = lambda(x) => x * x
```

### 泛型函数
支持泛型参数：
```lua
function identity<T>(x)
    return x
end
```

### 异步函数 (Async/Await)
```lua
async function fetchData()
    local data = await http.get("https://example.com")
    return data
end
```

### 管道操作符
- `|>` (正向管道): 将左侧结果作为右侧函数的第一个参数
- `<|` (反向管道): 将右侧结果作为左侧函数的第一个参数
- `|?>` (安全管道): 如果左侧为 nil 则短路返回 nil

```lua
"hello" |> string.upper |> print -- PRINT("HELLO")
```

## 6. 面向对象编程 (OOP)

### 类定义 (class)
支持单继承、接口实现、静态成员、访问控制：
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
    private radius

    function Circle(r)
        self.radius = r
    end

    function draw()
        print("Drawing circle: " .. self.radius)
    end

    static function create(r)
        return new Circle(r)
    end
end
```

### 属性 (Getter/Setter)
```lua
class Person
    private _age = 0

    public get age()
        return self._age
    end

    public set age(v)
        if v >= 0 then self._age = v end
    end
end
```

### 访问修饰符
- `public`: 公开（默认）
- `private`: 仅类内部可见
- `protected`: 类及其子类可见

### 其他修饰符
- `static`: 静态成员
- `abstract`: 抽象类/方法（需子类实现）
- `final`: 最终类/方法（不可继承/重写）
- `sealed`: 密封类

### Concept (概念)
定义类型约束（类似 C++ Concepts）：
```lua
concept Addable(a, b)
    return a + b
end
```

## 7. 错误处理

### Try-Catch-Finally
```lua
try
    error("Something wrong")
catch(e)
    print("Caught error: " .. e)
finally
    print("Cleanup")
end
```

## 8. 模块与命名空间

### Namespace
组织代码的命名空间：
```lua
namespace MyLib {
    function test() print("test") end
}
MyLib::test()
```

### Using
引入命名空间或成员：
```lua
using namespace MyLib;
test()
```

### Export
导出模块成员：
```lua
export function myFunc() end
export class MyClass ... end
```

## 9. 元编程与宏

### 自定义关键字 (keyword)
定义新的关键字语法：
```lua
keyword unless(cond)
    if not cond then
        local _ = ... -- 代码块
    end
end
```

### 自定义命令 (command)
定义 Shell 风格的命令：
```lua
command echo(...)
    print(...)
end
echo "Hello" "World"
```

### 自定义运算符 (operator)
重载或定义新运算符：
```lua
operator ++(x)
    return x + 1
end
```
使用 `$$++(val)` 调用。

### 预处理指令
- `$if condition`: 条件编译
- `$define NAME value`: 定义宏常量
- `$include "file"`: 包含文件
- `$alias`: 定义别名

## 10. 内联汇编 (ASM)
使用 `asm` 块直接编写虚拟机指令：
```lua
asm(
    LOADI R0 100;
    RETURN1 R0;
)
```
更多详情请参考 `ASM_TUTORIAL.md`。

## 11. 标准库扩展

### fs (文件系统)
提供文件操作：`ls`, `mkdir`, `rm`, `stat`, `exists`, `isdir` 等。

### process (进程管理) (Linux)
提供进程操作：`open`, `read`, `write`, `getpid`。

### ptr (指针操作)
直接内存操作：`malloc`, `free`, `copy`, `ptr(addr)`.

### vm (虚拟机控制)
控制 VM 行为：`vm.execute`, `vm.compile`.
