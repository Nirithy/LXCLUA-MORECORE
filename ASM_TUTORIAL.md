# XCLua Inline Assembly Tutorial

XCLua provides a powerful inline assembly feature that allows you to inject raw Lua virtual machine instructions directly into your Lua code. This is useful for performance optimization, low-level manipulation, or implementing features not expressible in standard Lua.

## Basic Syntax

Inline assembly is written inside an `asm(...)` block. Instructions are separated by semicolons (`;`).

```lua
asm(
    LOADI R0 100;
    RETURN1 R0;
)
```

## Operands and Modifiers

The assembler supports various modifiers to make it easier to work with Lua's internal structures.

### Registers
*   **Raw Registers**: `R0`, `R1`, `R2`, ... (Direct register access)
*   **Local Variables**: `$varname` (Resolves to the register assigned to the local variable `varname`)
*   **Allocated Registers**: See `newreg` below.

### Constants
*   **Integers**: `#123` (Used for immediate operands like `LOADI`)
*   **Floats**: `#3.14` (Used for `LOADF`)
*   **Strings**: `#"string"` (Adds string to constant table and returns index)
*   **Add to Constant Pool**:
    *   `#K 123`: Adds integer 123 to constant pool and returns index.
    *   `#KF 3.14`: Adds float 3.14 to constant pool and returns index.

### Upvalues
*   **Upvalue Index**: `^varname` (Resolves to the upvalue index of `varname`)

### Labels and Jumps
*   **Define Label**: `:label_name`
*   **Reference Label**: `@label_name` (Returns PC of label)
*   **Current PC**: `@`

### Special Values
*   `!freereg`: Current top of the stack (first free register).
*   `!pc`: Current instruction pointer.
*   `!nk`: Number of constants.

## Directives and Pseudo-instructions

### `newreg name`
Allocates a new register on the stack and defines `name` as an alias for it. This ensures the compiler knows the register is in use.

```lua
asm(
    newreg temp;
    LOADI temp 42;
)
```

### `getglobal reg "name"`
Loads a global variable into a register.

```lua
asm(
    newreg r;
    getglobal r "print";
)
```

### `setglobal reg "name"`
Sets a global variable from a register.

```lua
asm(
    newreg val;
    LOADI val 100;
    setglobal val "my_global";
)
```

### `def name value`
Defines a local assembly constant.

```lua
asm(
    def MY_CONST 10;
    LOADI R0 MY_CONST;
)
```

### `jmpx @label` / `JMP @label`
Jumps to a label. `jmpx` is a helper that explicitly calculates relative offsets, but standard `JMP` also supports `@label` correctly.

```lua
asm(
    :loop;
    ...
    JMP @loop;
)
```

### Debugging
*   `_print "msg" val`: Prints a message and optional value during compilation.
*   `_assert cond`: Asserts a condition at compile time.

## Examples

### 1. Simple Math
```lua
local result
asm(
    newreg a;
    newreg b;
    LOADI a 10;
    LOADI b 20;
    ADD a a b;
    MOVE $result a;
)
print(result) -- 30
```

### 2. Global Function Call
```lua
asm(
    newreg func;
    newreg arg;
    getglobal func "print";
    LOADK arg #"Hello from ASM!";
    CALL func 2 1; -- 2 args (func + arg), 1 result (void)
)
```

### 3. Custom Loop
```lua
local sum = 0
asm(
    newreg cnt;
    newreg acc;
    LOADI cnt 5;
    LOADI acc 0;

    :loop;
    ADD acc acc cnt;
    SUBK cnt cnt #K 1; -- Subtract constant 1

    -- Check if cnt > 0
    -- LT A B k: if (R[A] < R[B]) ~= k then pc++
    -- We want: if cnt > 0 goto loop
    -- Use LEI (Less Equal Immediate)? Or just check sign?
    -- Let's use simple logic: if cnt == 0 then break
    EQI cnt 0 0; -- if cnt == 0 then skip next (JMP)
    JMP @loop;

    MOVE $sum acc;
)
print(sum) -- 15
```
