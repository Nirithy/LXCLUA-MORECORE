# Lua 5.5 (官方) vs XCLUA (自定义) GC与内存管理对比报告

## 1. 概述 (Overview)

本报告对比了当前代码库 (XCLUA, 自称 "Lua 5.5 Custom") 与官方 Lua 代码库 (GitHub `master` 分支, 版本号 5.5.1) 在垃圾回收 (GC) 和内存管理方面的差异。

## 2. 内存管理 (Memory Management)

### 官方 Lua 5.5.1
- **标准分配器**: 官方实现主要依赖 `luaM_realloc_`、`luaM_free_` 等函数，这些函数直接封装了用户提供的内存分配器 (通常是 `realloc`)。
- **无内置内存池**: 小对象的分配直接由底层分配器处理，没有内置的小对象优化机制。

### XCLUA (本项目)
- **内置小对象内存池 (Small Object Memory Pool)**:
  - 在 `lmem.c` 中实现了 `luaM_poolalloc`、`luaM_poolfree` 等函数。
  - 在 `global_State` (`lstate.h`) 中增加了 `MemPoolArena` 和 `MemPool` 结构。
  - **机制**: 针对 1024 字节以下的小对象，使用预分配的空闲链表 (Free List) 进行管理，减少了频繁调用系统 `realloc` 的开销。
  - **集成**: `luaM_poolgc` 函数被钩入 GC 循环中，用于在垃圾回收时整理和收缩内存池。

## 3. 垃圾回收 (Garbage Collection)

### 共同点
- **分代 GC (Generational GC)**: 两者都实现了分代垃圾回收机制 (在 Lua 5.4 中引入)。
  - 使用 `G_NEW`, `G_SURVIVAL`, `G_OLD1`, `G_OLD` 等标记来管理对象生命周期。
  - 支持 `KGC_INC` (增量模式) 和 `KGC_GEN` (分代模式)。

### 差异点 (XCLUA 特有)
- **内存池清理钩子**: XCLUA 在 `lgc.c` 的 `singlestep` 函数 (`GCSswpend` 阶段) 和 `luaC_fullgc` 中调用了 `luaM_poolgc(L)`。这意味着每次完整的 GC 循环结束或紧急 GC 时，都会尝试释放内存池中多余的缓存块。

## 4. 对象结构与混淆 (Object Structure & Obfuscation)

XCLUA 为了支持代码混淆和虚拟机保护，对核心数据结构进行了扩展：

- **Proto (函数原型)**:
  - 增加了 `difierline_mode` (混淆模式), `difierline_magicnum` (魔数), `difierline_data` (混淆数据)。
  - 增加了 `vm_code_table` 指针，用于指向自定义的 VM 指令表。

- **global_State (全局状态)**:
  - 增加了 `vm_code_list` 链表，用于管理加载的混淆代码表。

## 5. 总结

XCLUA 基于 Lua 5.4/5.5 的核心架构，但在内存管理上引入了**专用的内存池**以优化小对象性能，并在 GC 流程中加入了对该内存池的维护。此外，为了支持 VM 保护和代码混淆，核心对象结构 (`Proto`) 被显著扩展。
