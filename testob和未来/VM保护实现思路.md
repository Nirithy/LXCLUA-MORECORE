# VM保护实现思路总结

## 当前实现状态

已完成部分：
1. ✅ VMCodeTable结构定义（lobfuscate.h）
2. ✅ Proto扩展字段vm_code_table（lobject.h）
3. ✅ global_State扩展vm_code_list（lstate.h/c）
4. ✅ VM代码表管理函数（lobfuscate.c）
   - luaO_registerVMCode
   - luaO_findVMCode
   - luaO_freeAllVMCode
   - decryptVMInst（解密函数）
5. ✅ luaO_executeVM核心解释循环（lobfuscate.c:3057-3638）
   - 数据移动: NOP, HALT, MOVE, LOAD, STORE
   - 算术运算: ADD, SUB, MUL, DIV, MOD, IDIV, UNM
   - 位运算: BAND, BOR, BXOR, BNOT, SHL, SHR
   - 跳转比较: JMP, JEQ, JNE, JLT, JLE, JGT, JGE
   - 逻辑: NOT, LEN, CONCAT
   - 表操作: NEWTABLE, GETTABLE, SETTABLE, GETFIELD, SETFIELD
   - Upvalue: GETUPVAL, SETUPVAL
   - 函数调用: CALL, RET, CLOSURE, SELF
   - 复杂指令(TAILCALL, VARARG, FORLOOP等)回退到原生VM
6. ✅ lvm.c添加VM保护检测入口（startfunc标签后）
7. ✅ luaO_vmProtect注册VM代码到全局表
8. ✅ ldump.c VM代码序列化（写入字节码文件）
9. ✅ lundump.c VM代码反序列化（从字节码加载）

## 已完成的核心任务

### 1. 实现luaO_executeVM核心解释循环

**位置**：lobfuscate.c 约3057行

**需要实现的功能**：
```c
int luaO_executeVM (lua_State *L, Proto *f) {
  /* 获取VM代码表 */
  VMCodeTable *vm = f->vm_code_table;
  if (vm == NULL) return 0;  /* 非VM保护函数 */
  
  int pc = 0;
  
  /* 主执行循环 */
  while (pc < vm->size) {
    /* 1. 解密当前指令 */
    VMInstruction encrypted = vm->code[pc];
    VMInstruction decrypted = decryptVMInst(encrypted, vm->encrypt_key, pc);
    
    /* 2. 提取VM操作码并映射回Lua操作码 */
    int vm_op = VM_GET_OPCODE(decrypted);
    int lua_op = vm->reverse_map[vm_op];
    
    /* 3. 根据操作码执行相应操作 */
    switch (lua_op) {
      case OP_MOVE: {
        /* 实现寄存器移动 */
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        /* 访问Lua运行时栈并执行移动 */
        break;
      }
      
      case OP_LOADI: {
        /* 加载立即数 */
        int a = VM_GET_A(decrypted);
        int sbx = VM_GET_sBx(decrypted);
        /* 设置寄存器值 */
        break;
      }
      
      case OP_LOADK: {
        /* 加载常量 */
        int a = VM_GET_A(decrypted);
        int bx = VM_GET_Bx(decrypted);
        /* 从Proto->k获取常量并设置 */
        break;
      }
      
      case OP_ADD:
      case OP_SUB:
      case OP_MUL:
      case OP_DIV: {
        /* 算术运算 */
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        int c = VM_GET_C(decrypted);
        /* 执行运算并存储结果 */
        break;
      }
      
      case OP_EQ:
      case OP_LT:
      case OP_LE: {
        /* 比较操作 */
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        int k = VM_GET_k(decrypted);
        /* 执行比较，根据结果决定是否跳转 */
        break;
      }
      
      case OP_JMP: {
        /* 跳转指令 */
        int sj = VM_GET_sJ(decrypted);
        pc += sj;
        continue;  /* 跳过pc++ */
      }
      
      case OP_CALL: {
        /* 函数调用 */
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        int c = VM_GET_C(decrypted);
        /* 准备调用帧并调用函数 */
        break;
      }
      
      case OP_RETURN: {
        /* 返回指令 */
        int a = VM_GET_A(decrypted);
        int b = VM_GET_B(decrypted);
        /* 清理栈帧并返回 */
        return 0;
      }
      
      default:
        /* 其他操作码 - 可以调用Lua原生解释器处理 */
        break;
    }
    
    pc++;
  }
  
  return 0;
}
```

**关键技术点**：
- 需要访问Lua运行时的栈和寄存器
- 需要与lvm.c中的执行环境集成
- 需要处理各种指令格式（ABC, ABx, AsBx, Ax等）

### 2. 修改lvm.c添加VM保护检测

**位置**：lvm.c 中的 luaV_execute 函数

**需要修改的位置**：
```c
void luaV_execute (lua_State *L, CallInfo *ci) {
  LClosure *cl;
  TValue *k;
  StkId base;
  const Instruction *pc;
  int trap;
  
  /* ... 现有代码 ... */
  
  cl = ci_func(ci);
  
  /* 添加VM保护检测 */
  if (cl->p->difierline_mode & OBFUSCATE_VM_PROTECT) {
    /* 调用VM解释器 */
    luaO_executeVM(L, cl->p);
    return;  /* 执行完毕直接返回 */
  }
  
  /* 原始执行逻辑继续 */
  k = cl->p->k;
  pc = ci->u.l.savedpc;
  /* ... 剩余代码 ... */
}
```

### 3. 完善luaO_vmProtect函数

**当前问题**：生成了VM代码但没有注册到全局表

**需要修改**：
```c
int luaO_vmProtect (lua_State *L, Proto *f, unsigned int seed) {
  /* ... 现有代码 ... */
  
  /* 注册VM代码到全局表（新增） */
  VMCodeTable *vt = luaO_registerVMCode(L, f, 
                                       ctx->vm_code, 
                                       ctx->vm_code_size,
                                       ctx->encrypt_key,
                                       ctx->reverse_map,
                                       seed);
  if (vt == NULL) {
    CFF_LOG("注册VM代码失败");
    luaO_freeVMContext(ctx);
    return -1;
  }
  
  /* 标记为VM保护 */
  f->difierline_mode |= OBFUSCATE_VM_PROTECT;
  
  /* ... 剩余代码 ... */
}
```

### 4. 修改序列化支持

**ldump.c**：保存VM代码到字节码文件
**lundump.c**：从字节码文件加载VM代码

需要添加新的块类型来存储VM保护数据。

## 编译测试步骤

1. **编译核心模块**：
   ```
   C:\Users\ruilo\AppData\Local\Android\Sdk\ndk\27.0.12077973\ndk-build.cmd APP_MODULES=lua
   ```

2. **测试VM保护功能**：
   ```bash
   # 测试所有保护功能（包括VM）
   ./lua -e "difierline_mode=255" test_script.lua
   
   # 仅测试VM保护
   ./lua -e "difierline_mode=128" test_script.lua
   
   # 测试NESTED+VM组合
   ./lua -e "difierline_mode=159" test_script.lua
   ```

## 关键挑战

1. **运行时集成**：VM解释器需要深度集成Lua运行时环境
2. **栈管理**：正确管理Lua的栈和寄存器
3. **错误处理**：保持与原生Lua相同的错误处理机制
4. **性能优化**：避免VM解释带来过多性能开销

## 优先级建议

1. 先实现核心算术和控制流指令（MOVE, LOAD*, ADD/SUB/MUL/DIV, JMP, CALL, RETURN）
2. 再实现比较和逻辑指令（EQ/LT/LE, TEST）
3. 最后实现复杂指令（TABLE操作, CLOSURE等）

这样可以快速验证VM保护的基本功能，然后逐步完善。