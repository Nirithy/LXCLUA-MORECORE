# LXCLUA-NCore

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-C23-blue.svg)]()
[![Platform](https://img.shields.io/badge/Platform-Cross--Platform-green.svg)]()

A high-performance embedded scripting engine based on **Lua 5.5 (Custom)** with enhanced security features, extended libraries, and optimized bytecode compilation.

[中文文档 (Chinese Documentation)](README_CN.md)

---

## Features

### Core Enhancements

- **Secure Compilation**: Dynamic OPcode mapping, timestamp encryption, SHA-256 integrity verification.
- **Custom VM**: Implements XCLUA instruction set with optimized dispatch.
- **Syntax Extensions**: Modern language features including Classes, Switch, Try-Catch, Arrow Functions, Pipe Operators, and more.
- **Shell-like Conditions**: Built-in support for shell-style test expressions (e.g., `[ -f "file.txt" ]`).

### Extension Modules

| Module | Description |
|--------|------------------------|
| `json` | Built-in JSON parsing/serialization |
| `lclass` | OOP support (classes, inheritance, interfaces) |
| `lbitlib` | Bitwise operations |
| `lboolib` | Boolean enhancements |
| `ludatalib` | Binary data serialization |
| `lsmgrlib` | Memory management utilities |
| `process` | Process management (Linux only) |
| `http` | HTTP client/server & Socket |
| `thread` | Multithreading support |
| `fs` | File system operations |
| `struct` | C-style structs & arrays |

---

## Syntax Extensions

LXCLUA-NCore introduces modern language features to extend Lua 5.5.

### 1. Extended Operators

Supports compound assignments, increment/decrement, spaceship operator, null coalescing, optional chaining, pipe operators, and walrus operator.

```lua
-- Compound Assignment & Increment
local a = 10
a += 5          -- a = 15
a++             -- a = 16

-- Spaceship Operator (-1, 0, 1)
local cmp = 10 <=> 20  -- -1

-- Null Coalescing
local val = nil
local res = val ?? "default"  -- "default"

-- Optional Chaining
local config = { server = { port = 8080 } }
local port = config?.server?.port  -- 8080
local timeout = config?.client?.timeout  -- nil

-- Pipe Operator
local function double(x) return x * 2 end
local result = 10 |> double  -- 20

-- Safe Pipe (skips if nil)
local maybe_nil = nil
local _ = maybe_nil |?> print  -- (does nothing)

-- Walrus Operator (Assignment Expression)
local x
if (x := 100) > 50 then
    print(x) -- 100
end
```

### 2. Enhanced Strings

- **Interpolation**: `${var}` or `${[expr]}` inside strings.
- **Raw Strings**: Prefixed with `_raw`, ignores escape sequences.

```lua
local name = "World"
print("Hello, ${name}!")  -- Hello, World!

local calc = "1 + 1 = ${[1+1]}"  -- 1 + 1 = 2

local path = _raw"C:\Windows\System32"
```

### 3. Function Features

Supports arrow functions, lambdas, C-style definitions, generics, and async/await.

```lua
-- Arrow Function
local add = (a, b) => a + b
local log = ->(msg) { print("[LOG]: " .. msg) }

-- Lambda Expression
local sq = lambda(x): x * x

-- C-style Function
int sum(int a, int b) {
    return a + b;
}

-- Generic Function
local function Factory(T)(val)
    return { type = T, value = val }
end
local obj = Factory("int")(99)

-- Async/Await
async function fetchData(url)
    -- local data = await http.get(url) -- (Requires async runtime)
    return "data"
end
```

### 4. Object-Oriented Programming (OOP)

Complete class and interface system with modifiers (`private`, `public`, `static`, `final`, `abstract`, `sealed`) and properties (`get`/`set`).

```lua
interface Drawable
    function draw(self)
end

class Shape implements Drawable
    function draw(self)
        -- abstract-like behavior
    end
end

-- Sealed Class (cannot be extended)
sealed class Circle extends Shape
    private _radius = 0

    function __init__(self, r)
        self._radius = r
    end

    -- Property with Getter/Setter
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

### 5. Structs & Types

```lua
-- Struct
struct Point {
    int x;
    int y;
}
local p = Point()
p.x = 10

-- Concept (Type Predicate)
concept IsPositive(x)
    return x > 0
end
-- Or single expression form
concept IsEven(x) = x % 2 == 0

-- SuperStruct (Enhanced Table Definition)
superstruct MetaPoint [
    x: 0,
    y: 0,
    ["move"]: function(self, dx, dy)
        self.x = self.x + dx
        self.y = self.y + dy
    end
]

-- Enum
enum Color {
    Red,
    Green,
    Blue = 10
}

-- Destructuring
local data = { x = 1, y = 2 }
local take { x, y } = data
```

### 6. Control Flow

```lua
-- Switch Statement
switch (val) do
    case 1:
        print("One")
        break
    default:
        print("Other")
end

-- When Statement (Pattern Matching)
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

-- With Statement
local ctx = { val = 10 }
with (ctx) {
    print(val) -- 10
}

-- Namespace & Using
namespace MyLib {
    function test() return "test" end
}
using namespace MyLib; -- Import all
-- using MyLib::test;  -- Import specific member
```

### 7. Shell-like Tests

Built-in conditional tests using `[ ... ]` syntax.

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

### 8. Metaprogramming & Macros

```lua
-- Custom Command
command echo(msg)
    print(msg)
end
echo "Hello World"

-- Custom Operator
operator ++ (x)
    return x + 1
end
-- Call with $$ prefix
local res = $$++(10)

-- Preprocessor Directives
$define DEBUG 1
$alias CONST_VAL = 100
$type MyInt = int

$if DEBUG
    print("Debug mode")
$else
    print("Release mode")
$end

$declare g_var: MyInt

-- Object Macro
local x = 10
local obj = $object(x) -- {x=10}
```

### 9. Inline ASM

Write VM instructions directly. Use `newreg` to allocate registers safely.
Supports pseudo-instructions like `rep`, `_if`, `_print`.

```lua
asm(
    newreg r0
    LOADI r0 100

    -- Compile-time loop
    rep 5 {
        ADDI r0 r0 1
    }

    -- Conditional assembly
    _if 1
       _print "Compiling this block"
    _endif

    -- Embedding data
    -- db 1, 2, 3, 4
    -- str "RawData"

    RETURN1 r0
)
```

---

## Build & Test

### Build

```bash
# Linux
make linux

# Windows (MinGW)
make mingw
```

### Verification

Run the test suite to verify all features:

```bash
./lxclua tests/verify_docs_full.lua
./lxclua tests/test_parser_features.lua
./lxclua tests/test_advanced_parser.lua
```

## License

[MIT License](LICENSE).
Lua original code Copyright © PUC-Rio.
