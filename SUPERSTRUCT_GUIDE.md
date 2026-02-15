# SuperStruct 使用指南

## 1. 简介 (Introduction)

`SuperStruct` 是 Lua 核心中引入的一种原生数据结构，旨在作为标准 Table 的高性能、内存紧凑型替代方案，特别适用于作为元表（Metatable）或结构化数据对象。

它填补了 Table（灵活但内存开销大）和 Userdata（紧凑但需要 C 绑定）之间的空白，提供了一种原生支持的、基于数组的键值对存储结构。

## 2. 核心特性 (Core Features)

*   **原生语法支持**：拥有专门的 `superstruct` 关键字定义语法。
*   **内存紧凑**：内部使用线性数组（Vector）存储键值对，没有哈希表的额外开销（如节点指针、哈希桶）。
*   **构建优化**：编译器生成专门的 `OP_NEWSUPER` 和 `OP_SETSUPER` 指令，构建速度极快。
*   **元表能力**：原生支持作为元表使用，支持 `__index` 等元方法。
*   **动态性**：虽然基于数组，但支持动态添加字段（会自动扩容）。

## 3. 基础用法 (Basic Usage)

### 3.1 定义 SuperStruct

使用 `superstruct` 关键字定义：

```lua
superstruct Point [
    x : 0,
    y : 0,
    name : "Point"
]
```

这会创建一个名为 `Point` 的全局 SuperStruct 对象。

### 3.2 访问与修改

用法与普通 Table 非常相似：

```lua
-- 读取
print(Point.x)      -- 输出: 0
print(Point.name)   -- 输出: "Point"

-- 修改
Point.x = 100
Point.name = "Start"

-- 添加新字段 (支持，但建议在定义时规划好以避免重分配)
Point.z = 50
```

### 3.3 实例化 (作为原型)

通常 `SuperStruct` 用作原型或元表。虽然它本身是一个对象，但你可以通过 `table.clone` (如果支持) 或自定义构造函数来复制它，或者更常见的是，将其用作 Table 的元表。

## 4. 高级用法 (Advanced Usage)

### 4.1 作为元表 (Metatable)

这是 `SuperStruct` 最强大的场景。由于元表通常是静态的且包含少量方法，使用 `SuperStruct` 可以显著减少内存占用并提高缓存局部性。

```lua
superstruct MyMeta [
    __index : function(t, k)
        return "Default: " .. tostring(k)
    end,
    version : "1.0"
]

local t = {}
setmetatable(t, MyMeta)

print(t.foo) -- 输出: Default: foo
```

### 4.2 性能特性与实现细节

*   **存储结构**：`Header + Name + Size + Data Array`。数据数组按 `[Key1, Value1, Key2, Value2, ...]` 顺序线性存储。
*   **查找算法**：**线性搜索 (O(N))**。
    *   **优势**：对于小规模数据（< 20 个字段），线性扫描比哈希计算和链表遍历更快，且 CPU 缓存命中率极高。
    *   **劣势**：对于包含大量字段（如 > 100 个）的对象，查找性能会显著下降。
*   **适用场景**：元表、配置对象、小型结构体、原型对象。
*   **不适用场景**：大型字典、需要频繁随机访问的大型数据集。

### 4.3 限制

*   **迭代器**：默认**不支持** `pairs()` 遍历。标准库的 `pairs` 仅适用于 `LUA_TTABLE`。如果需要遍历，必须在 SuperStruct 中显式定义 `__pairs` 元方法。
*   **扩容开销**：虽然支持添加新字段，但因为是数组存储，扩容需要 `realloc` 和内存拷贝，成本比链式哈希表高。

## 5. 评分 (Rating)

**综合评分：8.5 / 10**

*   **设计理念 (9/10)**：填补了 Lua 中轻量级对象的空缺，设计简洁直接。
*   **性能 (9/10)**：在目标场景（元表、小对象）下性能卓越，初始化和访问开销极低。
*   **内存效率 (10/10)**：无哈希开销，极其紧凑。
*   **易用性 (8/10)**：原生语法支持很好，但缺乏默认迭代器 (`pairs`) 是一个小的使用门槛。
*   **通用性 (6/10)**：由于 O(N) 的查找复杂度，不适合通用的大规模数据存储，但这正是为了特定优化而做的权衡。

**总结**：`SuperStruct` 是 Lua 高级用户的利器，用于优化元表和构建高性能的对象系统。
