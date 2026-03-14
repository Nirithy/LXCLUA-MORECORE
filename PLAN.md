# LXCLUA-NCore 极致性能与极致安全双引擎重构计划书 (Tiered & Hybrid Architecture)

本计划书旨在彻底重构 LXCLUA-NCore 的字节码逻辑与虚拟机架构。核心目标是实现**既要极致性能（High-Performance JIT/AOT）又要极致安全（VMP/CFF 高级混淆）**的双引擎架构。

重构的核心思想是引入**强类型中间表示 (SSA IR)** 作为解耦层，并在运行时引入**双引擎虚拟机 (Dual-Engine VM)** 机制，通过编译期注解（Pragma）将代码分流至“性能管线”或“安全管线”。

---

## 阶段一：架构解耦与统一中间表示 (IR) 层的建立

本阶段的目标是打破当前 `源代码 -> AST -> 字节码` 的单线编译流程，在 AST 和 Bytecode 之间引入独立于具体指令格式的中间表示层（IR）。

- [x] **1.1 定义 SSA IR 数据结构 (`lir.h` / `lir.c`)**
  - [x] 设计类似于 LLVM IR 的三地址码格式，例如 `IR_ADD v1, v2, v3`。
  - [x] 定义基本块（Basic Block）结构，包含入口、出口、前驱（Predecessors）和后继（Successors）指针。
  - [x] 实现 IR 节点的内存分配与管理机制。

- [ ] **1.2 改造前端解析器 (`lparser.c` / `llexer_compiler.c`)**
  - [ ] 修改现有的 AST 生成逻辑，使其不再直接发射（Emit）64位定长字节码。
  - [ ] 实现 `AST -> IR` 的遍历转换生成器。
  - [ ] 支持编译期注解或宏（例如 `@optimize("speed")` 或 `@optimize("obfuscate")`），并将其作为 Metadata 附加到对应函数的 IR 对象上。

- [ ] **1.3 实现基础的 IR 优化 Pass**
  - [ ] **常量折叠 (Constant Folding)**：在编译期计算常量表达式（如 `1 + 2` -> `3`）。
  - [ ] **常量传播 (Constant Propagation)**：将常量赋值传播到后续使用处。
  - [ ] **死代码消除 (Dead Code Elimination)**：移除不可达的 IR 块（如 `if (false)` 分支）。
  - [ ] 实现 IR 层面的控制流图 (CFG) 构建与分析工具。

---

## 阶段二：安全管线 (Security Pipeline) 与高级混淆重构

本阶段的目标是将现有的混淆逻辑（如 `lobfuscate.c` 中的控制流扁平化、虚假块等）上移至 IR 层，并增强 VMP 保护机制。

- [ ] **2.1 IR 级别的控制流扁平化 (IR-CFF)**
  - [ ] 重写 `cff.lua` 或 `lobfuscate.c` 中的 CFF 逻辑，使其直接操作 IR 的基本块。
  - [ ] 实现基于 IR 的基本块洗牌（Block Shuffle）。
  - [ ] 在 IR 图中注入虚假基本块（Bogus Blocks）和不透明谓词（Opaque Predicates）。

- [ ] **2.2 终极虚拟机保护 (VMP) 的指令生成**
  - [ ] 设计动态操作码（Opcode Mutation）生成机制，为每个受保护的函数生成独一无二的随机指令映射表。
  - [ ] 将 IR 编译为一维的、基于纯数学驱动状态机（State Machine Array）的加密字节码。
  - [ ] 实现常量字符串的深层加密（例如使用 AES-CTR 模式或自定义的纯数学解密算法）。

- [ ] **2.3 变量与环境虚拟化 (Variable Virtualization)**
  - [ ] 在 IR 层拦截对 `_G` 和局部变量的访问。
  - [ ] 将变量访问重定向到混淆后的虚拟内存数组（Virtual Memory Array），消除原生的寄存器和全局查找指令。

---

## 阶段三：性能管线 (Performance Pipeline) 与指令集升级

本阶段的目标是针对标记为高性能的代码，生成极致优化的执行流。

- [ ] **3.1 设计变长混合指令集 (Variable-Length ISA)**
  - [ ] 修改 `lopcodes.h`，摒弃单一的 64 位定长指令。
  - [ ] 引入 32位/64位/96位 混合指令编码，高频短指令使用短编码以提升 I-Cache 命中率。
  - [ ] 设计超级指令（Super-Instructions），例如将 `GETTABLE` + `CALL` 融合成单条 `GETTABLE_CALL`。

- [ ] **3.2 `IR -> 优化字节码` 的代码生成器**
  - [ ] 实现将优化后的 IR 转换为上述变长指令集的发射器（Emitter）。
  - [ ] 优化寄存器分配算法，尽量减少寄存器溢出（Spill）。

- [ ] **3.3 JIT/AOT 编译集成 (TCC/WASM 深度融合)**
  - [ ] 对于极端的计算密集型函数，利用已有的 `tcc` 模块，在加载期将 IR 直接转译为 C 代码并编译为原生机器码。
  - [ ] 探索将部分 IR 编译为 WASM 并在内置的 `wasm3` 引擎中执行的路径。

---

## 阶段四：双引擎虚拟机 (Dual-Engine VM) 的实现

本阶段的目标是重构核心执行器 `lvm.c`，使其能够同时支持高速执行和高安全执行，并在两者间无缝切换。

- [ ] **4.1 高速分发器 (Fast Dispatcher - `lvm_fast.c`)**
  - [ ] 使用 C 语言的 **计算型 goto (Computed Goto / Direct Threading)** 替换传统的 `switch-case`，消除分支预测惩罚。
  - [ ] 专门负责执行由性能管线生成的变长/超级指令。

- [ ] **4.2 安全分发器 (Secure Dispatcher - `lvm_secure.c`)**
  - [ ] 实现一个复杂的、包含多层混淆和状态机步进的执行循环。
  - [ ] 专门负责解释由安全管线生成的、高度加密的动态指令集。

- [ ] **4.3 双引擎边界切换机制 (Boundary Switch)**
  - [ ] 在 `lvm.c` 中实现上下文管理，当高速函数调用安全函数（或反之）时，能够安全地挂起当前执行引擎，切换到另一个引擎。
  - [ ] 确保在切换时，Lua 的调用栈（Call Stack）、闭包环境（Upvalues）和异常处理（Try-Catch）保持一致且零拷贝。

---

## 阶段五：序列化格式重构与全面测试

本阶段的目标是更新字节码的 Dump/Undump 流程，并确保整个双引擎架构的稳定性。

- [ ] **5.1 二进制格式加固 (`ldump.c` / `lundump.c`)**
  - [ ] 放弃标准 Lua 头部，引入自定义魔数、编译时间戳和动态执行密钥。
  - [ ] 对常量表、局部变量信息等区块进行 AES 分块加密。
  - [ ] 实现加载时的 SHA-256 内存实时完整性校验。

- [ ] **5.2 基础功能集成测试**
  - [ ] 编写测试用例，验证简单的数学运算和控制流在 IR 层的正确转换。
  - [ ] 跑通基础的双引擎互相调用测试（Mock 指令）。

- [ ] **5.3 进阶特性与回归测试**
  - [ ] 验证所有现代语法扩展（类、Switch、Try-Catch、箭头函数等）在双管线下的正确性。
  - [ ] 进行大规模的压力测试和内存泄漏分析（尤其是边界切换时的栈管理）。

---
*End of Document. 极致性能与极致安全双引擎重构计划书。*
