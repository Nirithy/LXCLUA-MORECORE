/*
** $Id: lobfuscate.h $
** Control Flow Flattening Obfuscation for Lua bytecode
** DifierLine
*/

#ifndef lobfuscate_h
#define lobfuscate_h

#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"

/*
** =======================================================
** 控制流扁平化混淆模块
** =======================================================
** 
** 控制流扁平化（Control Flow Flattening）是一种代码混淆技术，
** 其核心思想是将原始的控制流结构（如顺序执行、条件分支、循环等）
** 转换为一个统一的dispatcher-switch结构，使得静态分析变得困难。
**
** 原理：
** 1. 将代码划分为基本块（Basic Blocks）
** 2. 为每个基本块分配一个唯一的状态ID
** 3. 添加一个dispatcher循环，根据状态变量跳转到对应的基本块
** 4. 将原始的跳转指令转换为状态变量赋值
**
** 变换前:
**   block1:
**     ...
**     if cond then goto block2 else goto block3
**   block2:
**     ...
**     goto block4
**   block3:
**     ...
**     goto block4
**   block4:
**     ...
**
** 变换后:
**   state = initial_state
**   while true do
**     switch(state) {
**       case 1: ... state = cond ? 2 : 3; break;
**       case 2: ... state = 4; break;
**       case 3: ... state = 4; break;
**       case 4: ... return; break;
**     }
**   end
*/


/*
** 混淆模式标志位（可组合使用）
*/
#define OBFUSCATE_NONE              0       /* 不进行混淆 */
#define OBFUSCATE_CFF               (1<<0)  /* 控制流扁平化 */
#define OBFUSCATE_BLOCK_SHUFFLE     (1<<1)  /* 基本块随机打乱顺序 */
#define OBFUSCATE_BOGUS_BLOCKS      (1<<2)  /* 插入虚假基本块 */
#define OBFUSCATE_STATE_ENCODE      (1<<3)  /* 状态值编码混淆 */
#define OBFUSCATE_NESTED_DISPATCHER (1<<4)  /* 嵌套分发器（多层状态机） */
#define OBFUSCATE_OPAQUE_PREDICATES (1<<5)  /* 不透明谓词（恒真/恒假条件） */
#define OBFUSCATE_FUNC_INTERLEAVE   (1<<6)  /* 函数交织（虚假函数路径） */
#define OBFUSCATE_VM_PROTECT        (1<<7)  /* VM保护（自定义虚拟机指令集） */


/*
** 基本块结构体
** 用于在扁平化过程中表示一个代码基本块
*/
typedef struct BasicBlock {
  int start_pc;           /* 基本块起始PC */
  int end_pc;             /* 基本块结束PC（不包含） */
  int state_id;           /* 分配的状态ID */
  int original_target;    /* 原始跳转目标（如果是跳转块） */
  int fall_through;       /* 顺序执行的下一个块ID（-1表示无） */
  int cond_target;        /* 条件跳转目标块ID（-1表示无） */
  int is_entry;           /* 是否为入口块 */
  int is_exit;            /* 是否为出口块（包含RETURN指令） */
} BasicBlock;


/*
** 扁平化上下文结构体
** 保存扁平化过程中的所有状态信息
*/
typedef struct CFFContext {
  lua_State *L;             /* Lua状态 */
  Proto *f;                 /* 原始函数原型 */
  BasicBlock *blocks;       /* 基本块数组 */
  int num_blocks;           /* 基本块数量 */
  int block_capacity;       /* 基本块数组容量 */
  Instruction *new_code;    /* 新生成的代码 */
  int new_code_size;        /* 新代码大小 */
  int new_code_capacity;    /* 新代码容量 */
  int state_reg;            /* 状态变量寄存器（内层） */
  int outer_state_reg;      /* 外层状态变量寄存器（嵌套分发器模式） */
  int opaque_reg1;          /* 不透明谓词临时寄存器1 */
  int opaque_reg2;          /* 不透明谓词临时寄存器2 */
  int func_id_reg;          /* 函数ID寄存器（函数交织模式） */
  int dispatcher_pc;        /* dispatcher位置 */
  int outer_dispatcher_pc;  /* 外层dispatcher位置（嵌套分发器模式） */
  int num_groups;           /* 基本块分组数量（嵌套分发器模式） */
  int *group_starts;        /* 每个分组的起始块索引（嵌套分发器模式） */
  int num_fake_funcs;       /* 虚假函数数量（函数交织模式） */
  unsigned int seed;        /* 随机数种子 */
  int obfuscate_flags;      /* 混淆标志 */
} CFFContext;


/*
** 扁平化元数据结构体
** 保存用于解扁平化的元数据信息
*/
typedef struct CFFMetadata {
  int enabled;              /* 是否启用了扁平化 */
  int num_blocks;           /* 基本块数量 */
  int state_reg;            /* 状态变量寄存器 */
  int dispatcher_pc;        /* dispatcher位置 */
  int *block_mapping;       /* 状态ID到原始PC的映射 */
  int original_size;        /* 原始代码大小 */
  unsigned int seed;        /* 用于恢复的随机种子 */
} CFFMetadata;


/*
** =======================================================
** 公共API函数声明
** =======================================================
*/


/*
** 对函数原型进行控制流扁平化
** @param L Lua状态
** @param f 要处理的函数原型
** @param flags 混淆标志位组合
** @param seed 随机种子（用于可重复的混淆）
** @return 成功返回0，失败返回错误码
** 
** 功能描述：
** - 分析字节码，识别基本块边界
** - 构建控制流图
** - 生成扁平化后的代码
** - 更新函数原型中的代码
** - 如果提供了log_path，输出详细的转换日志到文件
*/
LUAI_FUNC int luaO_flatten (lua_State *L, Proto *f, int flags, unsigned int seed,
                            const char *log_path);


/*
** 对函数原型进行反扁平化（恢复原始控制流）
** @param L Lua状态
** @param f 要处理的函数原型
** @param metadata 扁平化元数据
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** - 根据元数据恢复原始的基本块结构
** - 重建原始的跳转指令
** - 移除dispatcher代码
*/
LUAI_FUNC int luaO_unflatten (lua_State *L, Proto *f, CFFMetadata *metadata);


/*
** 序列化扁平化元数据（用于保存到字节码文件）
** @param L Lua状态
** @param ctx 扁平化上下文
** @param buffer 输出缓冲区
** @param size 缓冲区大小（输入输出参数）
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** - 将扁平化相关的元数据序列化为二进制格式
** - 包括基本块映射、状态寄存器等信息
*/
LUAI_FUNC int luaO_serializeMetadata (lua_State *L, CFFContext *ctx, 
                                       void *buffer, size_t *size);


/*
** 反序列化扁平化元数据（从字节码文件加载）
** @param L Lua状态
** @param buffer 输入缓冲区
** @param size 缓冲区大小
** @param metadata 输出元数据结构
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** - 从二进制数据恢复扁平化元数据
** - 验证数据完整性
*/
LUAI_FUNC int luaO_deserializeMetadata (lua_State *L, const void *buffer, 
                                         size_t size, CFFMetadata *metadata);


/*
** 释放扁平化元数据占用的内存
** @param L Lua状态
** @param metadata 要释放的元数据
*/
LUAI_FUNC void luaO_freeMetadata (lua_State *L, CFFMetadata *metadata);


/*
** =======================================================
** 内部辅助函数声明
** =======================================================
*/


/*
** 识别并构建基本块
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
*/
LUAI_FUNC int luaO_identifyBlocks (CFFContext *ctx);


/*
** 检查指令是否为基本块终结指令
** @param op 操作码
** @return 是终结指令返回1，否则返回0
*/
LUAI_FUNC int luaO_isBlockTerminator (OpCode op);


/*
** 检查指令是否为跳转指令
** @param op 操作码
** @return 是跳转指令返回1，否则返回0
*/
LUAI_FUNC int luaO_isJumpInstruction (OpCode op);


/*
** 获取跳转指令的目标PC
** @param inst 指令
** @param pc 当前PC
** @return 跳转目标PC
*/
LUAI_FUNC int luaO_getJumpTarget (Instruction inst, int pc);


/*
** 生成dispatcher代码
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
*/
LUAI_FUNC int luaO_generateDispatcher (CFFContext *ctx);


/*
** 随机打乱基本块顺序
** @param ctx 扁平化上下文
*/
LUAI_FUNC void luaO_shuffleBlocks (CFFContext *ctx);


/*
** 编码状态值（增加混淆程度）
** @param state 原始状态值
** @param seed 随机种子
** @return 编码后的状态值
*/
LUAI_FUNC int luaO_encodeState (int state, unsigned int seed);


/*
** 解码状态值
** @param encoded_state 编码后的状态值
** @param seed 随机种子
** @return 原始状态值
*/
LUAI_FUNC int luaO_decodeState (int encoded_state, unsigned int seed);


/*
** 生成带虚假参数的NOP指令
** @param seed 随机种子（用于生成随机参数）
** @return 生成的NOP指令
*/
LUAI_FUNC Instruction luaO_createNOP (unsigned int seed);


/*
** 生成嵌套分发器代码
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** 嵌套分发器使用两层状态机：
** - 外层分发器：根据outer_state选择内层分发器
** - 内层分发器：根据inner_state选择实际的基本块
** 
** 结构示例：
**   outer_switch(outer_state):
**     case 0: inner_switch_0(inner_state)
**     case 1: inner_switch_1(inner_state)
**     ...
*/
LUAI_FUNC int luaO_generateNestedDispatcher (CFFContext *ctx);


/*
** 不透明谓词类型枚举
** 用于选择不同类型的恒真/恒假条件
*/
typedef enum {
  OP_ALWAYS_TRUE,     /* 恒真谓词 */
  OP_ALWAYS_FALSE     /* 恒假谓词 */
} OpaquePredicateType;


/*
** 生成不透明谓词代码
** @param ctx 扁平化上下文
** @param type 谓词类型（恒真或恒假）
** @param seed 随机种子指针（用于选择谓词变体）
** @param true_target 条件为真时跳转的PC偏移
** @param false_target 条件为假时跳转的PC偏移
** @return 成功返回生成的指令数，失败返回-1
**
** 功能描述：
** 生成一段字节码，其中包含一个看起来是条件判断但结果始终确定的代码。
** 
** 不透明谓词示例：
** - 恒真: x*(x+1) % 2 == 0 （连续整数乘积必为偶数）
** - 恒真: x*x >= 0 （平方数非负）
** - 恒假: x*(x+1) % 2 == 1 （连续整数乘积不可能为奇数）
*/
LUAI_FUNC int luaO_emitOpaquePredicate (CFFContext *ctx, OpaquePredicateType type,
                                         unsigned int *seed);


/*
** =======================================================
** VM保护模块
** =======================================================
**
** VM保护将Lua字节码转换为自定义虚拟机指令集，
** 然后在运行时由内置解释器执行。
**
** 原理：
** 1. 定义一套与Lua原生操作码不同的自定义VM操作码
** 2. 将原始Lua字节码转换为自定义VM指令
** 3. 用随机密钥对操作码和操作数进行加密
** 4. 运行时由VM解释器解密并执行
**
** 这使得逆向分析必须首先理解自定义VM的指令集，
** 大大增加了分析难度。
*/


/*
** VM操作码定义（自定义指令集）
** 操作码经过重新编号和重排，与Lua原生操作码不同
*/
typedef enum {
  /* 数据移动类 */
  VM_OP_NOP = 0,        /* 空操作 */
  VM_OP_LOAD,           /* 加载常量/立即数 */
  VM_OP_MOVE,           /* 寄存器移动 */
  VM_OP_STORE,          /* 存储到内存 */
  
  /* 算术运算类 */
  VM_OP_ADD,            /* 加法 */
  VM_OP_SUB,            /* 减法 */
  VM_OP_MUL,            /* 乘法 */
  VM_OP_DIV,            /* 除法 */
  VM_OP_MOD,            /* 取模 */
  VM_OP_POW,            /* 幂运算 */
  VM_OP_UNM,            /* 取负 */
  VM_OP_IDIV,           /* 整除 */
  
  /* 位运算类 */
  VM_OP_BAND,           /* 按位与 */
  VM_OP_BOR,            /* 按位或 */
  VM_OP_BXOR,           /* 按位异或 */
  VM_OP_BNOT,           /* 按位取反 */
  VM_OP_SHL,            /* 左移 */
  VM_OP_SHR,            /* 右移 */
  
  /* 比较跳转类 */
  VM_OP_JMP,            /* 无条件跳转 */
  VM_OP_JEQ,            /* 相等跳转 */
  VM_OP_JNE,            /* 不等跳转 */
  VM_OP_JLT,            /* 小于跳转 */
  VM_OP_JLE,            /* 小于等于跳转 */
  VM_OP_JGT,            /* 大于跳转 */
  VM_OP_JGE,            /* 大于等于跳转 */
  
  /* 函数调用类 */
  VM_OP_CALL,           /* 函数调用 */
  VM_OP_RET,            /* 函数返回 */
  VM_OP_TAILCALL,       /* 尾调用 */
  
  /* 表操作类 */
  VM_OP_NEWTABLE,       /* 创建表 */
  VM_OP_GETTABLE,       /* 获取表元素 */
  VM_OP_SETTABLE,       /* 设置表元素 */
  VM_OP_GETFIELD,       /* 获取字段 */
  VM_OP_SETFIELD,       /* 设置字段 */
  
  /* 闭包/Upvalue类 */
  VM_OP_CLOSURE,        /* 创建闭包 */
  VM_OP_GETUPVAL,       /* 获取upvalue */
  VM_OP_SETUPVAL,       /* 设置upvalue */
  
  /* 特殊指令 */
  VM_OP_CONCAT,         /* 字符串连接 */
  VM_OP_LEN,            /* 获取长度 */
  VM_OP_NOT,            /* 逻辑非 */
  VM_OP_FORLOOP,        /* for循环 */
  VM_OP_FORPREP,        /* for准备 */
  VM_OP_VARARG,         /* 可变参数 */
  VM_OP_SELF,           /* self调用 */
  VM_OP_SETLIST,        /* 设置列表 */
  
  /* 扩展/保留 */
  VM_OP_EXT1,           /* 扩展指令1 */
  VM_OP_EXT2,           /* 扩展指令2 */
  VM_OP_HALT,           /* 停止执行 */
  
  VM_OP_COUNT           /* 操作码数量 */
} VMOpCode;


/*
** VM指令格式
** 每条VM指令为64位，包含：
** - 操作码 (8位, 加密后)
** - 操作数A (16位)
** - 操作数B (16位)
** - 操作数C (16位)
** - 标志位 (8位)
*/
typedef uint64_t VMInstruction;

/* VM指令字段访问宏 */
#define VM_GET_OP(inst)    ((int)(((inst) >> 56) & 0xFF))
#define VM_GET_A(inst)     ((int)(((inst) >> 40) & 0xFFFF))
#define VM_GET_B(inst)     ((int)(((inst) >> 24) & 0xFFFF))
#define VM_GET_C(inst)     ((int)(((inst) >> 8) & 0xFFFF))
#define VM_GET_FLAGS(inst) ((int)((inst) & 0xFF))

#define VM_MAKE_INST(op, a, b, c, flags) \
  (((uint64_t)(op) << 56) | ((uint64_t)(a) << 40) | \
   ((uint64_t)(b) << 24) | ((uint64_t)(c) << 8) | (uint64_t)(flags))


/*
** VM保护上下文
*/
typedef struct VMProtectContext {
  lua_State *L;             /* Lua状态 */
  Proto *f;                 /* 原始函数原型 */
  VMInstruction *vm_code;   /* VM指令数组 */
  int vm_code_size;         /* VM指令数量 */
  int vm_code_capacity;     /* VM指令容量 */
  uint64_t encrypt_key;     /* 加密密钥 */
  int *opcode_map;          /* 操作码映射表 (Lua -> VM) */
  int *reverse_map;         /* 反向映射表 (VM -> Lua) */
  unsigned int seed;        /* 随机种子 */
} VMProtectContext;


/*
** VM运行时状态
*/
typedef struct VMState {
  VMInstruction *code;      /* VM代码 */
  int code_size;            /* 代码大小 */
  int pc;                   /* 程序计数器 */
  uint64_t decrypt_key;     /* 解密密钥 */
  int *opcode_map;          /* 操作码反向映射 */
  lua_State *L;             /* Lua状态 */
  CallInfo *ci;             /* 当前调用信息 */
} VMState;


/*
** VM代码表（全局链表节点）
** 用于存储已转换的VM指令，支持运行时执行
*/
typedef struct VMCodeTable {
  struct Proto *proto;       /* 关联的函数原型 */
  VMInstruction *code;       /* VM指令数组（已加密） */
  int size;                  /* 指令数量 */
  int capacity;              /* 数组容量 */
  uint64_t encrypt_key;      /* 加密密钥 */
  int *reverse_map;          /* VM操作码 -> Lua操作码 反向映射 */
  unsigned int seed;         /* 随机种子 */
  struct VMCodeTable *next;  /* 链表下一节点 */
} VMCodeTable;


/*
** 注册VM代码到全局表
** @param L Lua状态
** @param p 函数原型
** @param code VM指令数组
** @param size 指令数量
** @param key 加密密钥
** @param reverse_map 反向映射表
** @param seed 随机种子
** @return 成功返回VMCodeTable指针，失败返回NULL
*/
LUAI_FUNC VMCodeTable *luaO_registerVMCode (lua_State *L, struct Proto *p,
                                            VMInstruction *code, int size,
                                            uint64_t key, int *reverse_map,
                                            unsigned int seed);

/*
** 查找函数关联的VM代码表
** @param L Lua状态
** @param p 函数原型
** @return 找到返回VMCodeTable指针，未找到返回NULL
*/
LUAI_FUNC VMCodeTable *luaO_findVMCode (lua_State *L, struct Proto *p);

/*
** 释放所有VM代码表
** @param L Lua状态
*/
LUAI_FUNC void luaO_freeAllVMCode (lua_State *L);


/*
** 初始化VM保护上下文
** @param L Lua状态
** @param f 函数原型
** @param seed 随机种子
** @return 上下文指针，失败返回NULL
*/
LUAI_FUNC VMProtectContext *luaO_initVMContext (lua_State *L, Proto *f, 
                                                  unsigned int seed);


/*
** 释放VM保护上下文
** @param ctx 要释放的上下文
*/
LUAI_FUNC void luaO_freeVMContext (VMProtectContext *ctx);


/*
** 将Lua字节码转换为VM指令
** @param ctx VM保护上下文
** @return 成功返回0，失败返回错误码
*/
LUAI_FUNC int luaO_convertToVM (VMProtectContext *ctx);


/*
** 执行VM保护的代码
** @param L Lua状态
** @param f 函数原型（包含VM代码）
** @return 执行结果
*/
LUAI_FUNC int luaO_executeVM (lua_State *L, Proto *f);


/*
** 对函数进行VM保护
** @param L Lua状态
** @param f 要保护的函数原型
** @param seed 随机种子
** @return 成功返回0，失败返回错误码
*/
LUAI_FUNC int luaO_vmProtect (lua_State *L, Proto *f, unsigned int seed);


#endif /* lobfuscate_h */
