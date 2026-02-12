
/*
** $Id: lobfuscate.c $
** Control Flow Flattening Obfuscation for Lua bytecode
** DifierLine
*/

#define lobfuscate_c
#define LUA_CORE

#include "lprefix.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

#include "lua.h"
#include "lobfuscate.h"
#include "lmem.h"
#include "lopcodes.h"
#include "lobject.h"
#include "lstate.h"
#include "ltm.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lvm.h"
#include "ltable.h"
#include "lfunc.h"
#include "lstring.h"
#include "lclass.h"
#include <math.h>

/* 全局日志文件指针 - 由 luaO_flatten 设置 */
static FILE *g_cff_log_file = NULL;

/*
** 写入CFF调试日志
** @param fmt 格式字符串
** @param ... 可变参数
*/
static void CFF_LOG(const char *fmt, ...) {
  if (g_cff_log_file == NULL) return;
  
  va_list args;
  va_start(args, fmt);
  fprintf(g_cff_log_file, "[CFF] ");
  vfprintf(g_cff_log_file, fmt, args);
  fprintf(g_cff_log_file, "\n");
  fflush(g_cff_log_file);  /* 立即刷新确保日志写入 */
  va_end(args);
}

/* 获取操作码名称（用于调试输出） */
static const char* getOpName(OpCode op) {
  static const char* names[] = {
    "MOVE", "LOADI", "LOADF", "LOADK", "LOADKX", "LOADFALSE", "LFALSESKIP",
    "LOADTRUE", "LOADNIL", "GETUPVAL", "SETUPVAL", "GETTABUP", "GETTABLE",
    "GETI", "GETFIELD", "SETTABUP", "SETTABLE", "SETI", "SETFIELD", "NEWTABLE",
    "SELF", "ADDI", "ADDK", "SUBK", "MULK", "MODK", "POWK", "DIVK", "IDIVK",
    "BANDK", "BORK", "BXORK", "SHLI", "SHRI", "ADD", "SUB", "MUL", "MOD",
    "POW", "DIV", "IDIV", "BAND", "BOR", "BXOR", "SHL", "SHR", "SPACESHIP",
    "MMBIN", "MMBINI", "MMBINK", "UNM", "BNOT", "NOT", "LEN", "CONCAT",
    "CLOSE", "TBC", "JMP", "EQ", "LT", "LE", "EQK", "EQI", "LTI", "LEI",
    "GTI", "GEI", "TEST", "TESTSET", "CALL", "TAILCALL", "RETURN", "RETURN0",
    "RETURN1", "FORLOOP", "FORPREP", "TFORPREP", "TFORCALL", "TFORLOOP",
    "SETLIST", "CLOSURE", "VARARG", "GETVARG", "ERRNNIL", "VARARGPREP",
    "IS", "TESTNIL", "NEWCLASS", "INHERIT", "GETSUPER", "SETMETHOD",
    "SETSTATIC", "NEWOBJ", "GETPROP", "SETPROP", "INSTANCEOF", "IMPLEMENT",
    "SETIFACEFLAG", "ADDMETHOD", "SLICE", "NOP", "EXTRAARG"
  };
  if (op >= 0 && op < (int)(sizeof(names)/sizeof(names[0]))) {
    return names[op];
  }
  return "UNKNOWN";
}


/*
** =======================================================
** 内部常量定义
** =======================================================
*/

#define INITIAL_BLOCK_CAPACITY  16      /* 基本块数组初始容量 */
#define INITIAL_CODE_CAPACITY   64      /* 代码数组初始容量 */
#define CFF_MAGIC               0x43464600  /* "CFF\0" 元数据魔数 */
#define CFF_VERSION             1       /* 元数据版本号 */


/*
** =======================================================
** 辅助宏定义
** =======================================================
*/

/* 线性同余随机数生成器参数 */
#define LCG_A       1664525
#define LCG_C       1013904223

/* 生成下一个随机数 */
#define NEXT_RAND(seed) ((seed) = (LCG_A * (seed) + LCG_C))


/*
** =======================================================
** 内部辅助函数
** =======================================================
*/


/*
** 检查指令是否为基本块终结指令
** @param op 操作码
** @return 是终结指令返回1，否则返回0
**
** 终结指令包括：
** - 无条件跳转(JMP)
** - 条件跳转(EQ, LT, LE, TEST等)
** - 返回指令(RETURN, RETURN0, RETURN1, TAILCALL)
** - 循环指令(FORLOOP, FORPREP, TFORLOOP等)
*/
int luaO_isBlockTerminator (OpCode op) {
  switch (op) {
    case OP_JMP:
    case OP_EQ:
    case OP_LT:
    case OP_LE:
    case OP_EQK:
    case OP_EQI:
    case OP_LTI:
    case OP_LEI:
    case OP_GTI:
    case OP_GEI:
    case OP_TEST:
    case OP_TESTSET:
    case OP_TESTNIL:
    case OP_RETURN:
    case OP_RETURN0:
    case OP_RETURN1:
    case OP_TAILCALL:
    case OP_FORLOOP:
    case OP_FORPREP:
    case OP_TFORPREP:
    case OP_TFORLOOP:
      return 1;
    default:
      return 0;
  }
}


/*
** 检查指令是否为跳转指令
** @param op 操作码
** @return 是跳转指令返回1，否则返回0
*/
int luaO_isJumpInstruction (OpCode op) {
  switch (op) {
    case OP_JMP:
    case OP_FORLOOP:
    case OP_FORPREP:
    case OP_TFORPREP:
    case OP_TFORLOOP:
      return 1;
    default:
      return 0;
  }
}


/* 
** 每个基本块的最大指令数，超过此值将尝试拆分以增加控制流复杂度 
*/
#define MAX_BLOCK_SIZE  10

/*
** 检查指令是否为成对指令的第一部分（不应该在此指令后拆分基本块）
** @param op 操作码
** @return 是成对指令返回1，否则返回0
*/
static int isPairedInstruction (OpCode op) {
  switch (op) {
    /* 条件测试类：后跟 JMP */
    case OP_EQ: case OP_LT: case OP_LE:
    case OP_EQK: case OP_EQI: case OP_LTI:
    case OP_LEI: case OP_GTI: case OP_GEI:
    case OP_TEST: case OP_TESTSET: case OP_TESTNIL:
    case OP_IS: case OP_INSTANCEOF:
      return 1;
      
    /* 算术/位运算类：后跟 MMBIN 系列 (Lua 5.4) */
    case OP_ADDI: case OP_ADDK: case OP_SUBK: case OP_MULK:
    case OP_MODK: case OP_POWK: case OP_DIVK: case OP_IDIVK:
    case OP_BANDK: case OP_BORK: case OP_BXORK: case OP_SHLI:
    case OP_SHRI: case OP_ADD: case OP_SUB: case OP_MUL:
    case OP_MOD: case OP_POW: case OP_DIV: case OP_IDIV:
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL:
    case OP_SHR: case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_CONCAT:
      return 1;
      
    /* 其他特定对 */
    case OP_TFORCALL: /* 后跟 TFORLOOP */
    case OP_LOADKX:   /* 后跟 EXTRAARG */
    case OP_NEWTABLE: /* 可能后跟 EXTRAARG */
    case OP_SETLIST:  /* 可能后跟 EXTRAARG */
      return 1;
      
    default:
      return 0;
  }
}


/*
** 检查指令是否为条件测试指令（后面跟着跳转）
** @param op 操作码
** @return 是条件测试指令返回1，否则返回0
*/
static int isConditionalTest (OpCode op) {
  switch (op) {
    case OP_EQ:
    case OP_LT:
    case OP_LE:
    case OP_EQK:
    case OP_EQI:
    case OP_LTI:
    case OP_LEI:
    case OP_GTI:
    case OP_GEI:
    case OP_TEST:
    case OP_TESTSET:
    case OP_TESTNIL:
      return 1;
    default:
      return 0;
  }
}


/*
** 检查指令是否为返回指令
** @param op 操作码
** @return 是返回指令返回1，否则返回0
*/
static int isReturnInstruction (OpCode op) {
  switch (op) {
    case OP_RETURN:
    case OP_RETURN0:
    case OP_RETURN1:
    case OP_TAILCALL:
      return 1;
    default:
      return 0;
  }
}


/*
** 获取跳转指令的目标PC
** @param inst 指令
** @param pc 当前PC
** @return 跳转目标PC，如果不是跳转指令返回-1
*/
int luaO_getJumpTarget (Instruction inst, int pc) {
  OpCode op = GET_OPCODE(inst);
  switch (op) {
    case OP_JMP:
      return pc + 1 + GETARG_sJ(inst);
    case OP_FORLOOP:
    case OP_TFORLOOP:
      return pc + 1 - GETARG_Bx(inst);
    case OP_FORPREP:
    case OP_TFORPREP:
      return pc + 1 + GETARG_Bx(inst);
    default:
      return -1;
  }
}


/*
** 初始化扁平化上下文
** @param L Lua状态
** @param f 函数原型
** @param flags 混淆标志
** @param seed 随机种子
** @return 初始化的上下文，失败返回NULL
*/
static CFFContext *initContext (lua_State *L, Proto *f, int flags, unsigned int seed) {
  CFFContext *ctx = (CFFContext *)luaM_malloc_(L, sizeof(CFFContext), 0);
  if (ctx == NULL) return NULL;
  
  ctx->L = L;
  ctx->f = f;
  ctx->num_blocks = 0;
  ctx->block_capacity = INITIAL_BLOCK_CAPACITY;
  ctx->new_code = NULL;
  ctx->new_code_size = 0;
  ctx->new_code_capacity = 0;
  ctx->state_reg = f->maxstacksize;  /* 使用一个新的寄存器作为状态变量 */
  ctx->outer_state_reg = f->maxstacksize + 1;  /* 外层状态寄存器（嵌套模式） */
  ctx->opaque_reg1 = f->maxstacksize + 2;  /* 不透明谓词临时寄存器1 */
  ctx->opaque_reg2 = f->maxstacksize + 3;  /* 不透明谓词临时寄存器2 */
  ctx->func_id_reg = f->maxstacksize + 4;  /* 函数ID寄存器（函数交织模式） */
  ctx->dispatcher_pc = 0;
  ctx->outer_dispatcher_pc = 0;
  ctx->num_groups = 0;
  ctx->group_starts = NULL;
  ctx->num_fake_funcs = 0;
  ctx->seed = seed;
  ctx->obfuscate_flags = flags;
  ctx->skip_pc0 = 0;
  
  /* 分配基本块数组 */
  ctx->blocks = (BasicBlock *)luaM_malloc_(L, sizeof(BasicBlock) * ctx->block_capacity, 0);
  if (ctx->blocks == NULL) {
    luaM_free_(L, ctx, sizeof(CFFContext));
    return NULL;
  }
  
  return ctx;
}


/*
** 释放扁平化上下文
** @param ctx 要释放的上下文
*/
static void freeContext (CFFContext *ctx) {
  if (ctx == NULL) return;
  
  lua_State *L = ctx->L;
  
  if (ctx->blocks != NULL) {
    luaM_free_(L, ctx->blocks, sizeof(BasicBlock) * ctx->block_capacity);
  }
  
  if (ctx->new_code != NULL) {
    luaM_free_(L, ctx->new_code, sizeof(Instruction) * ctx->new_code_capacity);
  }
  
  if (ctx->group_starts != NULL) {
    luaM_free_(L, ctx->group_starts, sizeof(int) * (ctx->num_groups + 1));
  }
  
  luaM_free_(L, ctx, sizeof(CFFContext));
}


/*
** 添加基本块到上下文
** @param ctx 上下文
** @param start_pc 起始PC
** @param end_pc 结束PC
** @return 新块的索引，失败返回-1
*/
static int addBlock (CFFContext *ctx, int start_pc, int end_pc) {
  /* 检查是否需要扩展数组 */
  if (ctx->num_blocks >= ctx->block_capacity) {
    int new_capacity = ctx->block_capacity * 2;
    BasicBlock *new_blocks = (BasicBlock *)luaM_realloc_(
      ctx->L, ctx->blocks, 
      sizeof(BasicBlock) * ctx->block_capacity,
      sizeof(BasicBlock) * new_capacity
    );
    if (new_blocks == NULL) return -1;
    ctx->blocks = new_blocks;
    ctx->block_capacity = new_capacity;
  }
  
  int idx = ctx->num_blocks++;
  BasicBlock *block = &ctx->blocks[idx];
  block->start_pc = start_pc;
  block->end_pc = end_pc;
  block->state_id = idx;  /* 初始状态ID等于块索引 */
  block->original_target = -1;
  block->fall_through = -1;
  block->cond_target = -1;
  block->is_entry = (start_pc == 0);
  block->is_exit = 0;
  
  return idx;
}


/*
** 查找包含指定PC的基本块
** @param ctx 上下文
** @param pc 目标PC
** @return 块索引，未找到返回-1
*/
static int findBlockByPC (CFFContext *ctx, int pc) {
  for (int i = 0; i < ctx->num_blocks; i++) {
    if (pc >= ctx->blocks[i].start_pc && pc < ctx->blocks[i].end_pc) {
      return i;
    }
  }
  return -1;
}


/*
** 查找起始于指定PC的基本块
** @param ctx 上下文
** @param pc 目标PC
** @return 块索引，未找到返回-1
*/
static int findBlockStartingAt (CFFContext *ctx, int pc) {
  for (int i = 0; i < ctx->num_blocks; i++) {
    if (ctx->blocks[i].start_pc == pc) {
      return i;
    }
  }
  return -1;
}


/*
** =======================================================
** 基本块识别
** =======================================================
*/


/*
** 识别并构建基本块
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
**
** 算法：
** 1. 第一遍扫描：识别所有基本块的起始点
**    - 函数入口(PC=0)是起始点
**    - 跳转目标是起始点
**    - 跳转指令的下一条是起始点
** 2. 第二遍扫描：根据起始点划分基本块
** 3. 分析每个基本块的出口（跳转目标、顺序执行目标）
*/
int luaO_identifyBlocks (CFFContext *ctx) {
  Proto *f = ctx->f;
  int code_size = f->sizecode;
  
  CFF_LOG("========== 开始识别基本块 ==========");
  CFF_LOG("函数代码大小: %d 条指令", code_size);
  
  /* 打印原始指令 */
  CFF_LOG("--- 原始指令序列 ---");
  for (int pc = 0; pc < code_size; pc++) {
    Instruction inst = f->code[pc];
    OpCode op = GET_OPCODE(inst);
    int a = GETARG_A(inst);
    CFF_LOG("  [%03d] %s (A=%d, raw=0x%016llx)", pc, getOpName(op), a, (unsigned long long)inst);
  }
  
  /* 创建标记数组，标记哪些PC是基本块起始点 */
  if (code_size <= 0) return -1;
  lu_byte *is_leader = (lu_byte *)luaM_malloc_(ctx->L, (size_t)code_size, 0);
  if (is_leader == NULL) return -1;
  memset(is_leader, 0, (size_t)code_size);
  
  /* 第一条指令是入口点 */
  is_leader[0] = 1;
  
  /* 第一遍扫描：识别基本块起始点 */
  for (int pc = 0; pc < code_size; pc++) {
    Instruction inst = f->code[pc];
    OpCode op = GET_OPCODE(inst);
    
    /* 跳转指令的目标是起始点 */
    if (luaO_isJumpInstruction(op)) {
      int target = luaO_getJumpTarget(inst, pc);
      if (target >= 0 && target < code_size) {
        is_leader[target] = 1;
      }
      /* 跳转指令的下一条也是起始点（除非是无条件跳转或函数结束） */
      if (pc + 1 < code_size && op != OP_JMP) {
        is_leader[pc + 1] = 1;
      }
    }
    
    /* 条件测试指令后面跟着跳转，跳转后的下一条是起始点 */
    if (isConditionalTest(op)) {
      /* 条件测试指令会跳过下一条指令（通常是JMP） */
      if (pc + 2 < code_size) {
        is_leader[pc + 2] = 1;
      }
    }
    
    /* 返回指令的下一条是起始点（如果存在） */
    if (isReturnInstruction(op)) {
      if (pc + 1 < code_size) {
        is_leader[pc + 1] = 1;
      }
    }
  }
  
  /* 第二遍扫描：根据起始点划分基本块 */
  CFF_LOG("--- 划分基本块 ---");
  int block_start = 0;
  for (int pc = 1; pc <= code_size; pc++) {
    int force_split = 0;
    
    /* 检查是否超过最大块大小，如果是则尝试在此处拆分 */
    if (pc < code_size && (pc - block_start >= MAX_BLOCK_SIZE)) {
      /* 只有当前一个指令不是成对指令时，才允许在此处拆分 */
      OpCode prev_op = GET_OPCODE(f->code[pc-1]);
      if (!isPairedInstruction(prev_op)) {
        force_split = 1;
        CFF_LOG("  在 PC %d 强制拆分长基本块", pc);
      }
    }

    /* 当遇到新的起始点、强制拆分点或代码结束时，创建一个基本块 */
    if (pc == code_size || is_leader[pc] || force_split) {
      int idx = addBlock(ctx, block_start, pc);
      if (idx < 0) {
        luaM_free_(ctx->L, is_leader, code_size);
        return -1;
      }
      CFF_LOG("  块 %d: PC [%d, %d) (state_id=%d)", idx, block_start, pc, ctx->blocks[idx].state_id);
      block_start = pc;
    }
  }
  
  /* 第三遍：分析每个基本块的出口 */
  CFF_LOG("--- 分析基本块出口 ---");
  for (int i = 0; i < ctx->num_blocks; i++) {
    BasicBlock *block = &ctx->blocks[i];
    int last_pc = block->end_pc - 1;
    
    if (last_pc < 0 || last_pc >= code_size) continue;
    
    Instruction inst = f->code[last_pc];
    OpCode op = GET_OPCODE(inst);
    
    CFF_LOG("  块 %d 的最后指令 [%d]: %s", i, last_pc, getOpName(op));
    
    /* 检查是否为出口块 */
    if (isReturnInstruction(op)) {
      block->is_exit = 1;
      CFF_LOG("    -> 标记为出口块 (返回指令)");
    }
    
    /* 分析跳转目标 */
    if (luaO_isJumpInstruction(op)) {
      int target = luaO_getJumpTarget(inst, last_pc);
      if (target >= 0) {
        int target_block = findBlockStartingAt(ctx, target);
        block->original_target = target_block;
        CFF_LOG("    -> 跳转目标 PC=%d, 对应块 %d", target, target_block);
        
        /* 对于非无条件跳转，设置顺序执行目标 */
        if (op != OP_JMP) {
          int next_block = findBlockStartingAt(ctx, block->end_pc);
          block->fall_through = next_block;
          CFF_LOG("    -> 顺序执行目标块 %d", next_block);
        }
      }
    }
    
    /* 条件测试指令 */
    if (isConditionalTest(op)) {
      /* 条件为真时跳过下一条指令 */
      int skip_target = findBlockStartingAt(ctx, last_pc + 2);
      block->cond_target = skip_target;
      /* 条件为假时执行下一条（通常是JMP） */
      block->fall_through = findBlockStartingAt(ctx, block->end_pc);
      CFF_LOG("    -> 条件测试: 真->块%d (跳过JMP), 假->块%d (执行JMP)", 
              skip_target, block->fall_through);
    }
    
    /* 普通顺序执行 */
    if (!luaO_isBlockTerminator(op) && block->end_pc < code_size) {
      block->fall_through = findBlockStartingAt(ctx, block->end_pc);
      CFF_LOG("    -> 顺序执行到块 %d", block->fall_through);
    }
  }
  
  CFF_LOG("========== 基本块识别完成，共 %d 个块 ==========", ctx->num_blocks);
  
  luaM_free_(ctx->L, is_leader, code_size);
  return 0;
}


/*
** =======================================================
** 基本块打乱
** =======================================================
*/


/*
** 随机打乱基本块顺序
** @param ctx 扁平化上下文
**
** 使用Fisher-Yates算法打乱基本块顺序，
** 但保持入口块在第一位
*/
void luaO_shuffleBlocks (CFFContext *ctx) {
  if (ctx->num_blocks <= 2) return;  /* 块太少，不需要打乱 */
  
  unsigned int seed = ctx->seed;
  
  /* 从索引1开始打乱（保持入口块位置） */
  for (int i = ctx->num_blocks - 1; i > 1; i--) {
    NEXT_RAND(seed);
    int j = 1 + (seed % i);  /* j in [1, i) */
    
    /* 交换块i和块j的状态ID（不交换块本身，只交换执行顺序） */
    int temp = ctx->blocks[i].state_id;
    ctx->blocks[i].state_id = ctx->blocks[j].state_id;
    ctx->blocks[j].state_id = temp;
  }
  
  ctx->seed = seed;
}


/*
** =======================================================
** 状态编码
** =======================================================
*/


/*
** 编码状态值（增加混淆程度）
** @param state 原始状态值
** @param seed 随机种子
** @return 编码后的状态值
**
** 使用简单的线性变换进行编码，确保结果在 sC 参数范围内
** EQI 指令的 sC 参数是 16 位有符号数，范围 [-32767, 32768]
** 
** 为确保不同状态映射到不同编码值，使用基于种子的偏移和乘法：
** encoded = (state * prime + offset) mod range
** 其中 prime 与 range 互质，确保映射是一一对应的
*/
int luaO_encodeState (int state, unsigned int seed) {
  /* 使用固定范围和与之互质的乘数 */
  const int range = 30000;  /* 安全范围 */
  const int prime = 7919;   /* 质数，与 range 互质 */
  
  /* 使用种子生成偏移量 */
  int offset = (int)(seed % range);
  
  /* 线性变换：(state * prime + offset) mod range */
  /* 由于 prime 与 range 互质，这是一个置换（双射） */
  int encoded = ((state * prime) % range + offset) % range;
  if (encoded < 0) encoded += range;
  
  return encoded;
}


/*
** 解码状态值
** @param encoded_state 编码后的状态值
** @param seed 随机种子
** @return 原始状态值
**
** 这需要模逆运算，为简化实现，我们存储映射表
*/
int luaO_decodeState (int encoded_state, unsigned int seed) {
  /* 这个函数需要在元数据中存储映射表来实现 */
  /* 暂时返回原值，实际实现在反扁平化时使用映射表 */
  (void)seed;
  return encoded_state;
}


/*
** =======================================================
** 代码生成
** =======================================================
*/


/*
** 确保新代码数组有足够的空间
** @param ctx 上下文
** @param needed 需要的额外空间
** @return 成功返回0，失败返回-1
*/
static int ensureCodeCapacity (CFFContext *ctx, int needed) {
  int required = ctx->new_code_size + needed;
  
  if (required <= ctx->new_code_capacity) {
    return 0;
  }
  
  int new_capacity = ctx->new_code_capacity == 0 ? INITIAL_CODE_CAPACITY : ctx->new_code_capacity;
  while (new_capacity < required) {
    new_capacity *= 2;
  }
  
  Instruction *new_code;
  if (ctx->new_code == NULL) {
    new_code = (Instruction *)luaM_malloc_(ctx->L, sizeof(Instruction) * new_capacity, 0);
  } else {
    new_code = (Instruction *)luaM_realloc_(
      ctx->L, ctx->new_code,
      sizeof(Instruction) * ctx->new_code_capacity,
      sizeof(Instruction) * new_capacity
    );
  }
  
  if (new_code == NULL) return -1;
  
  ctx->new_code = new_code;
  ctx->new_code_capacity = new_capacity;
  return 0;
}


/*
** 添加指令到新代码
** @param ctx 上下文
** @param inst 指令
** @return 指令的PC，失败返回-1
*/
static int emitInstruction (CFFContext *ctx, Instruction inst) {
  if (ensureCodeCapacity(ctx, 1) != 0) return -1;
  
  int pc = ctx->new_code_size++;
  ctx->new_code[pc] = inst;
  return pc;
}


/*
** =======================================================
** 虚假块生成
** =======================================================
*/

/* 虚假块数量（基于真实块数量的倍数） */
#define BOGUS_BLOCK_RATIO  2  /* 每个真实块生成2个虚假块 */
#define BOGUS_BLOCK_MIN_INSTS  3  /* 虚假块最少指令数 */
#define BOGUS_BLOCK_MAX_INSTS  8  /* 虚假块最多指令数 */

/* 函数交织常量（在这里定义以便 luaO_generateDispatcher 使用） */
#define NUM_FAKE_FUNCTIONS  3   /* 虚假函数数量 */
#define FAKE_FUNC_BLOCKS    4   /* 每个虚假函数的块数量 */
#define FAKE_BLOCK_INSTS    5   /* 每个虚假块的指令数量 */

/* 前向声明（函数交织相关） */
static int emitFakeFunction (CFFContext *ctx, int func_id, unsigned int *seed,
                              int *entry_jmp_pc);
static int emitFakeFunctionBlocks (CFFContext *ctx, int func_id, unsigned int *seed,
                                    int entry_jmp_pc);

/* 二分查找分发器相关结构 */
typedef struct StateBlock {
  int state;      /* 编码后的状态值 */
  int block_idx;  /* 块索引（真实或虚假） */
} StateBlock;

/* 比较函数，用于状态排序 */
static int compareStateBlocks(const void *a, const void *b) {
  return ((StateBlock *)a)->state - ((StateBlock *)b)->state;
}

/* 递归生成二分查找树 */
static int emitBinarySearch (CFFContext *ctx, StateBlock *sb, int low, int high, int *all_block_jmp_pcs) {
  if (low == high) {
    /* 叶子节点：精确匹配状态 */
    CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (块%d)", 
            ctx->new_code_size, ctx->state_reg, sb[low].state, sb[low].block_idx);
    Instruction cmp_inst = CREATE_ABCk(OP_EQI, ctx->state_reg, int2sC(sb[low].state), 0, 1);
    if (emitInstruction(ctx, cmp_inst) < 0) return -1;
    
    Instruction jmp_inst = CREATE_sJ(OP_JMP, 0, 0);
    int jmp_pc = emitInstruction(ctx, jmp_inst);
    if (jmp_pc < 0) return -1;
    all_block_jmp_pcs[sb[low].block_idx] = jmp_pc;

    /* 混淆：在此处插入 NOP 是安全的 */
    if (ctx->obfuscate_flags & OBFUSCATE_RANDOM_NOP) {
      int num_nops = 1 + (ctx->seed % 2);
      for (int i = 0; i < num_nops; i++) {
        NEXT_RAND(ctx->seed);
        emitInstruction(ctx, luaO_createNOP(ctx->seed));
      }
    }
    return 0;
  } else {
    int mid = low + (high - low) / 2;
    /* 
    ** Lua LTI 语义: if ((R[A] < sB) ~= k) then pc++
    ** 当 k=1 时: if (R[A] < sB) 则执行下一条跳转指令，否则跳过它。
    */
    int next_state = sb[mid + 1].state;
    CFF_LOG("  [PC=%d] LTI R[%d], %d, k=1 (二分查找范围 [%d, %d])", 
            ctx->new_code_size, ctx->state_reg, next_state, low, high);
    Instruction lti = CREATE_ABCk(OP_LTI, ctx->state_reg, int2sC(next_state), 0, 1);
    if (emitInstruction(ctx, lti) < 0) return -1;
    
    /* 跳转到左半部分 */
    int jmp_left_pc = emitInstruction(ctx, CREATE_sJ(OP_JMP, 0, 0));
    if (jmp_left_pc < 0) return -1;

    /* 混淆：在此处插入 NOP 是安全的，因为上面是无条件跳转 */
    if (ctx->obfuscate_flags & OBFUSCATE_RANDOM_NOP) {
      int num_nops = 1 + (ctx->seed % 2);
      for (int i = 0; i < num_nops; i++) {
        NEXT_RAND(ctx->seed);
        emitInstruction(ctx, luaO_createNOP(ctx->seed));
      }
    }
    
    /* 跳转到右半部分 */
    int jmp_right_pc = emitInstruction(ctx, CREATE_sJ(OP_JMP, 0, 0));
    if (jmp_right_pc < 0) return -1;

    /* 混淆：在此处插入 NOP 是安全的 */
    if (ctx->obfuscate_flags & OBFUSCATE_RANDOM_NOP) {
      int num_nops = 1 + (ctx->seed % 2);
      for (int i = 0; i < num_nops; i++) {
        NEXT_RAND(ctx->seed);
        emitInstruction(ctx, luaO_createNOP(ctx->seed));
      }
    }
    
    /* 生成左半部分代码 */
    int left_start = ctx->new_code_size;
    if (emitBinarySearch(ctx, sb, low, mid, all_block_jmp_pcs) < 0) return -1;
    SETARG_sJ(ctx->new_code[jmp_left_pc], left_start - jmp_left_pc - 1);
    
    /* 生成右半部分代码 */
    int right_start = ctx->new_code_size;
    if (emitBinarySearch(ctx, sb, mid + 1, high, all_block_jmp_pcs) < 0) return -1;
    SETARG_sJ(ctx->new_code[jmp_right_pc], right_start - jmp_right_pc - 1);
    
    return 0;
  }
}

/* 虚假块数量（基于真实块数量的倍数） */
#define BOGUS_BLOCK_RATIO  2  /* 每个真实块生成2个虚假块 */
#define BOGUS_BLOCK_MIN_INSTS  3  /* 虚假块最少指令数 */
#define BOGUS_BLOCK_MAX_INSTS  8  /* 虚假块最多指令数 */

/* 生成一条随机的虚假指令 */
static Instruction generateBogusInstruction (CFFContext *ctx, unsigned int *seed) {
  int state_reg = ctx->state_reg;
  int max_reg = state_reg > 0 ? state_reg : 1;
  
  NEXT_RAND(*seed);
  int inst_type = *seed % 10;
  
  NEXT_RAND(*seed);
  int reg = (*seed % max_reg);
  if (reg < 0) reg = 0;
  
  NEXT_RAND(*seed);
  int value = (*seed % 1000) - 500;
  
  switch (inst_type) {
    case 0: return CREATE_ABx(OP_LOADI, reg, value + OFFSET_sBx);
    case 1: return CREATE_ABCk(OP_ADDI, reg, reg, int2sC(value % 100), 0);
    case 2: {
        NEXT_RAND(*seed);
        int src_reg = (*seed % max_reg);
        return CREATE_ABCk(OP_MOVE, reg, src_reg, 0, 0);
    }
    case 3: return CREATE_ABCk(OP_ADD, reg, (*seed % max_reg), ((*seed >> 8) % max_reg), 0);
    case 4: return CREATE_ABCk(OP_SUB, reg, (*seed % max_reg), ((*seed >> 8) % max_reg), 0);
    case 5: return CREATE_ABCk(OP_NOT, reg, (*seed % max_reg), 0, 0);
    case 6: return CREATE_ABCk(OP_LEN, reg, (*seed % max_reg), 0, 0);
    case 7: return CREATE_ABCk(OP_SHLI, reg, (*seed % max_reg), int2sC(value % 31), 0);
    case 8: return CREATE_ABCk(OP_BNOT, reg, (*seed % max_reg), 0, 0);
    default: return CREATE_ABx(OP_LOADI, reg, (*seed % 2000) + OFFSET_sBx);
  }
}

/*
** 发射状态转移指令
** @param ctx 上下文
** @param reg 状态寄存器
** @param next_state 下一个状态值
** @return 发射的指令数量
*/
static int emitStateTransition (CFFContext *ctx, int reg, int next_state) {
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
    /* 更加复杂的转换：使用加法混淆 */
    NEXT_RAND(ctx->seed);
    int delta = (ctx->seed % 100) - 50; /* -50 ~ 49 */
    if (emitInstruction(ctx, CREATE_ABx(OP_LOADI, reg, (next_state - delta) + OFFSET_sBx)) < 0) return -1;
    if (emitInstruction(ctx, CREATE_ABCk(OP_ADDI, reg, reg, int2sC(delta), 0)) < 0) return -1;
    return 2;
  } else {
    if (emitInstruction(ctx, CREATE_ABx(OP_LOADI, reg, next_state + OFFSET_sBx)) < 0) return -1;
    return 1;
  }
}


/* 生成一个虚假基本块的代码 */
static int emitBogusBlock (CFFContext *ctx, int bogus_state, unsigned int *seed) {
  int state_reg = ctx->state_reg;
  NEXT_RAND(*seed);
  int num_insts = BOGUS_BLOCK_MIN_INSTS + (*seed % (BOGUS_BLOCK_MAX_INSTS - BOGUS_BLOCK_MIN_INSTS + 1));
  CFF_LOG("  生成虚假块: state=%d, 指令数=%d", bogus_state, num_insts);
  for (int i = 0; i < num_insts; i++) {
    Instruction bogus_inst = generateBogusInstruction(ctx, seed);
    if (emitInstruction(ctx, bogus_inst) < 0) return -1;
  }
  NEXT_RAND(*seed);
  int next_state = bogus_state + 1 + (*seed % 3);
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
    next_state = luaO_encodeState(next_state, ctx->seed);
  }
  if (emitStateTransition(ctx, state_reg, next_state) < 0) return -1;
  int jmp_offset = ctx->dispatcher_pc - ctx->new_code_size - 1;
  Instruction jmp_inst = CREATE_sJ(OP_JMP, jmp_offset + OFFSET_sJ, 0);
  if (emitInstruction(ctx, jmp_inst) < 0) return -1;
  return 0;
}

/* 复制基本块并生成状态转移逻辑（公共代码） */
static int luaO_emitBlocksAndStubs (CFFContext *ctx, int *all_block_jmp_pcs, int *all_block_starts, 
                                     int num_bogus_blocks, unsigned int *bogus_seed) {
  Proto *f = ctx->f;
  int state_reg = ctx->state_reg;
  
  CFF_LOG("--- 复制基本块代码并生成存根 ---");
  
  for (int i = 0; i < ctx->num_blocks; i++) {
    BasicBlock *block = &ctx->blocks[i];
    all_block_starts[i] = ctx->new_code_size;
    
    CFF_LOG("块 %d: 原始PC [%d, %d), 新起始PC=%d", i, block->start_pc, block->end_pc, all_block_starts[i]);
    
    int last_pc = block->end_pc - 1;
    OpCode last_op = (last_pc >= block->start_pc) ? GET_OPCODE(f->code[last_pc]) : OP_NOP;
    
    /* 处理条件测试模式 */
    int has_cond_test = 0;
    int cond_test_pc = -1;
    if (last_op == OP_JMP && last_pc > block->start_pc) {
      OpCode prev_op = GET_OPCODE(f->code[last_pc - 1]);
      if (isConditionalTest(prev_op)) {
        has_cond_test = 1;
        cond_test_pc = last_pc - 1;
      }
    }
    
    /* 后置条件循环：发射存根在前，以便 backward jump 能够到达 */
    int loop_stub_pc = -1;
    if (last_op == OP_FORLOOP || last_op == OP_TFORLOOP) {
      int state_body = ctx->blocks[block->original_target].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) state_body = luaO_encodeState(state_body, ctx->seed);
      
      int skip_stub_pc = emitInstruction(ctx, CREATE_sJ(OP_JMP, 0, 0)); /* 跳过存根 */
      loop_stub_pc = ctx->new_code_size;
      emitStateTransition(ctx, state_reg, state_body);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      SETARG_sJ(ctx->new_code[skip_stub_pc], ctx->new_code_size - skip_stub_pc - 1);
    }
    
    /* 确定复制范围 */
    int copy_end = block->end_pc;
    if (has_cond_test) copy_end = cond_test_pc;
    else if (luaO_isJumpInstruction(last_op)) copy_end = last_pc;
    
    /* 复制指令 */
    for (int pc = block->start_pc; pc < copy_end; pc++) {
      if (ctx->skip_pc0 && pc == 0) {
        CFF_LOG("  跳过 PC 0 (已作为函数序言发射)");
        continue;
      }
      if (emitInstruction(ctx, f->code[pc]) < 0) return -1;
    }
    
    /* 处理块出口 */
    if (block->is_exit) {
      /* 出口块：复制返回指令 */
      for (int pc = copy_end; pc < block->end_pc; pc++) {
        if (emitInstruction(ctx, f->code[pc]) < 0) return -1;
      }
    } else if (has_cond_test) {
      /* 条件分支存根 */
      Instruction cond_inst = f->code[cond_test_pc];
      if (emitInstruction(ctx, cond_inst) < 0) return -1;
      
      int target_then = findBlockStartingAt(ctx, last_pc + 1);
      if (target_then < 0) target_then = block->fall_through;
      int target_else = findBlockStartingAt(ctx, luaO_getJumpTarget(f->code[last_pc], last_pc));
      
      int state_then = ctx->blocks[target_then].state_id;
      int state_else = ctx->blocks[target_else].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
        state_then = luaO_encodeState(state_then, ctx->seed);
        state_else = luaO_encodeState(state_else, ctx->seed);
      }
      
      int skip_then_pc = emitInstruction(ctx, CREATE_sJ(OP_JMP, 0, 0)); /* 跳过 then 分支 */
      emitStateTransition(ctx, state_reg, state_then);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      SETARG_sJ(ctx->new_code[skip_then_pc], ctx->new_code_size - skip_then_pc - 1);
      
      emitStateTransition(ctx, state_reg, state_else);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      
    } else if (last_op == OP_FORLOOP || last_op == OP_TFORLOOP) {
      Instruction loop_inst = f->code[last_pc];
      int state_next = ctx->blocks[block->fall_through].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) state_next = luaO_encodeState(state_next, ctx->seed);
      
      /* 计算跳转到 loop_stub_pc 的偏移量 */
      int current_pc = ctx->new_code_size;
      int bx = (current_pc + 1) - loop_stub_pc;
      SETARG_Bx(loop_inst, bx);
      emitInstruction(ctx, loop_inst);
      
      emitStateTransition(ctx, state_reg, state_next);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      
    } else if (last_op == OP_TFORPREP) {
      int a = GETARG_A(f->code[last_pc]);
      int state_call = ctx->blocks[block->original_target].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) state_call = luaO_encodeState(state_call, ctx->seed);
      emitInstruction(ctx, CREATE_ABCk(OP_TBC, a + 3, 0, 0, 0));
      emitStateTransition(ctx, state_reg, state_call);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));

    } else if (last_op == OP_FORPREP) {
      Instruction prep_inst = f->code[last_pc];
      int state_enter = ctx->blocks[block->fall_through].state_id;
      int state_skip = ctx->blocks[block->original_target].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
        state_enter = luaO_encodeState(state_enter, ctx->seed);
        state_skip = luaO_encodeState(state_skip, ctx->seed);
      }
      int prep_pc = ctx->new_code_size;
      emitInstruction(ctx, prep_inst);
      emitStateTransition(ctx, state_reg, state_enter);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      
      int skip_jump_pc = emitInstruction(ctx, CREATE_sJ(OP_JMP, 0, 0));
      int skip_stub_start = ctx->new_code_size;
      emitStateTransition(ctx, state_reg, state_skip);
      emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      
      SETARG_sJ(ctx->new_code[skip_jump_pc], ctx->new_code_size - skip_jump_pc - 1);
      SETARG_Bx(ctx->new_code[prep_pc], skip_stub_start - prep_pc - 1);
      
    } else {
      int next_block = (block->original_target >= 0) ? block->original_target : block->fall_through;
      if (next_block >= 0) {
        int next_state = ctx->blocks[next_block].state_id;
        if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) next_state = luaO_encodeState(next_state, ctx->seed);
        emitStateTransition(ctx, state_reg, next_state);
        emitInstruction(ctx, CREATE_sJ(OP_JMP, (ctx->dispatcher_pc - ctx->new_code_size - 1) + OFFSET_sJ, 0));
      }
    }
  }
  
  /* 生成虚假块 */
  for (int i = 0; i < num_bogus_blocks; i++) {
    all_block_starts[ctx->num_blocks + i] = ctx->new_code_size;
    if (emitBogusBlock(ctx, ctx->num_blocks + i, bogus_seed) < 0) return -1;
  }
  
  return 0;
}

/* 生成二分查找分发器 */
int luaO_generateBinaryDispatcher (CFFContext *ctx) {
  if (ctx->num_blocks == 0) return 0;
  int state_reg = ctx->state_reg;
  unsigned int bogus_seed = ctx->seed;
  CFF_LOG("========== 开始生成二分查找分发器 ==========");

  /* 稳定性保障：确保 VARARGPREP 始终作为第一条指令执行 */
  if (GET_OPCODE(ctx->f->code[0]) == OP_VARARGPREP) {
    CFF_LOG("检测到 VARARGPREP，将其保留在 PC 0");
    emitInstruction(ctx, ctx->f->code[0]);
    ctx->skip_pc0 = 1;
  }
  
  int num_bogus_blocks = (ctx->obfuscate_flags & OBFUSCATE_BOGUS_BLOCKS) ? ctx->num_blocks * BOGUS_BLOCK_RATIO : 0;
  int total_blocks = ctx->num_blocks + num_bogus_blocks;
  
  /* 查找入口状态 */
  int entry_block = 0;
  for (int i = 0; i < ctx->num_blocks; i++) {
    if (ctx->blocks[i].is_entry) {
      entry_block = i;
      break;
    }
  }
  int entry_state = ctx->blocks[entry_block].state_id;
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) entry_state = luaO_encodeState(entry_state, ctx->seed);
  emitStateTransition(ctx, state_reg, entry_state);
  
  ctx->dispatcher_pc = ctx->new_code_size;
  
  StateBlock *sb = (StateBlock *)luaM_malloc_(ctx->L, sizeof(StateBlock) * total_blocks, 0);
  if (sb == NULL) return -1;
  
  for (int i = 0; i < ctx->num_blocks; i++) {
    int s = ctx->blocks[i].state_id;
    if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) s = luaO_encodeState(s, ctx->seed);
    sb[i].state = s; sb[i].block_idx = i;
  }
  for (int i = 0; i < num_bogus_blocks; i++) {
    int s = ctx->num_blocks + i;
    if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) s = luaO_encodeState(s, ctx->seed);
    sb[ctx->num_blocks + i].state = s; sb[ctx->num_blocks + i].block_idx = ctx->num_blocks + i;
  }
  qsort(sb, total_blocks, sizeof(StateBlock), compareStateBlocks);
  
  int *all_block_jmp_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * total_blocks, 0);
  if (all_block_jmp_pcs == NULL) {
    luaM_free_(ctx->L, sb, sizeof(StateBlock) * total_blocks);
    return -1;
  }
  
  if (emitBinarySearch(ctx, sb, 0, total_blocks - 1, all_block_jmp_pcs) < 0) {
    luaM_free_(ctx->L, sb, sizeof(StateBlock) * total_blocks);
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    return -1;
  }
  luaM_free_(ctx->L, sb, sizeof(StateBlock) * total_blocks);
  
  emitInstruction(ctx, CREATE_sJ(OP_JMP, ctx->dispatcher_pc - ctx->new_code_size - 1 + OFFSET_sJ, 0));
  
  int *all_block_starts = (int *)luaM_malloc_(ctx->L, sizeof(int) * total_blocks, 0);
  if (all_block_starts == NULL) {
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    return -1;
  }
  
  if (luaO_emitBlocksAndStubs(ctx, all_block_jmp_pcs, all_block_starts, num_bogus_blocks, &bogus_seed) != 0) {
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    luaM_free_(ctx->L, all_block_starts, sizeof(int) * total_blocks);
    return -1;
  }
  
  /* 修正分发器跳转 */
  for (int i = 0; i < total_blocks; i++) {
    SETARG_sJ(ctx->new_code[all_block_jmp_pcs[i]], all_block_starts[i] - all_block_jmp_pcs[i] - 1);
  }
  
  luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
  luaM_free_(ctx->L, all_block_starts, sizeof(int) * total_blocks);
  return 0;
}




/*
** 生成dispatcher代码
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
**
** Dispatcher结构：
**   LOADI state_reg, initial_state   ; 初始化状态
** dispatcher_loop:
**   ; 对每个状态生成比较和跳转（包括真实块和虚假块）
**   EQI state_reg, state_0, k=1
**   JMP block_0
**   EQI state_reg, state_1, k=1
**   JMP block_1
**   ...
**   JMP dispatcher_loop              ; 默认跳回循环
*/
int luaO_generateDispatcher (CFFContext *ctx) {
  if (ctx->num_blocks == 0) return 0;
  
  int state_reg = ctx->state_reg;
  unsigned int bogus_seed = ctx->seed;
  
  CFF_LOG("========== 开始生成扁平化代码 ==========");
  CFF_LOG("状态寄存器: R[%d]", state_reg);

  /* 稳定性保障：确保 VARARGPREP 始终作为第一条指令执行 */
  if (GET_OPCODE(ctx->f->code[0]) == OP_VARARGPREP) {
    CFF_LOG("检测到 VARARGPREP，将其保留在 PC 0");
    emitInstruction(ctx, ctx->f->code[0]);
    ctx->skip_pc0 = 1;
  }
  
  /* 计算虚假块数量 */
  int num_bogus_blocks = 0;
  if (ctx->obfuscate_flags & OBFUSCATE_BOGUS_BLOCKS) {
    num_bogus_blocks = ctx->num_blocks * BOGUS_BLOCK_RATIO;
    CFF_LOG("启用虚假块: 将生成 %d 个虚假块", num_bogus_blocks);
  }
  
  int total_blocks = ctx->num_blocks + num_bogus_blocks;
  
  /* 生成初始化状态的指令 */
  /* 入口块的状态ID */
  int entry_state = 0;
  for (int i = 0; i < ctx->num_blocks; i++) {
    if (ctx->blocks[i].is_entry) {
      entry_state = ctx->blocks[i].state_id;
      CFF_LOG("入口块: 块%d, state_id=%d", i, entry_state);
      break;
    }
  }
  
  /* 如果启用了状态编码，编码初始状态 */
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
    entry_state = luaO_encodeState(entry_state, ctx->seed);
  }
  
  /* LOADI state_reg, entry_state */
  CFF_LOG("生成初始化指令: LOADI R[%d], %d", state_reg, entry_state);
  if (emitStateTransition(ctx, state_reg, entry_state) < 0) return -1;
  
  /* 如果启用函数交织，初始化函数ID寄存器 */
  int func_id_reg = ctx->func_id_reg;
  if (ctx->obfuscate_flags & OBFUSCATE_FUNC_INTERLEAVE) {
    ctx->num_fake_funcs = NUM_FAKE_FUNCTIONS;
    CFF_LOG("启用函数交织: 将生成 %d 个虚假函数", ctx->num_fake_funcs);
    /* 初始化函数ID为0（表示真实函数） */
    Instruction func_init = CREATE_ABx(OP_LOADI, func_id_reg, 0 + OFFSET_sBx);
    if (emitInstruction(ctx, func_init) < 0) return -1;
  }
  
  /* 记录dispatcher位置 */
  ctx->dispatcher_pc = ctx->new_code_size;
  CFF_LOG("分发器起始位置: PC=%d", ctx->dispatcher_pc);
  
  /* 为所有块（真实块+虚假块）分配跳转PC数组 */
  int *all_block_jmp_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * total_blocks, 0);
  if (all_block_jmp_pcs == NULL) return -1;
  
  /* 生成状态比较代码 - 真实块 */
  CFF_LOG("--- 生成状态比较代码（真实块）---");
  int opaque_counter = 0;
  unsigned int opaque_seed = ctx->seed ^ 0xDEADBEEF;
  
  for (int i = 0; i < ctx->num_blocks; i++) {
    /* 在每3个状态比较之间插入不透明谓词 */
    if ((ctx->obfuscate_flags & OBFUSCATE_OPAQUE_PREDICATES) && opaque_counter >= 3) {
      opaque_counter = 0;
      CFF_LOG("  插入恒真不透明谓词 @ PC=%d", ctx->new_code_size);
      
      /* 生成恒真谓词 */
      if (luaO_emitOpaquePredicate(ctx, OP_ALWAYS_TRUE, &opaque_seed) < 0) {
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
        return -1;
      }
      
      /* 恒真谓词后：条件为假时跳转到死代码（永不执行） */
      /* 生成一个跳过死代码的JMP，条件为真时跳过 */
      int dead_code_size = 3;  /* 死代码指令数 */
      Instruction skip_dead = CREATE_sJ(OP_JMP, dead_code_size + OFFSET_sJ, 0);
      if (emitInstruction(ctx, skip_dead) < 0) {
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
        return -1;
      }
      
      /* 生成死代码（永远不会执行但看起来像真实代码） */
      for (int d = 0; d < dead_code_size; d++) {
        Instruction dead = generateBogusInstruction(ctx, &opaque_seed);
        if (emitInstruction(ctx, dead) < 0) {
          luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
          return -1;
        }
      }
    }
    opaque_counter++;
    
    int state = ctx->blocks[i].state_id;
    
    if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
      state = luaO_encodeState(state, ctx->seed);
    }
    
    CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (真实块%d)", 
            ctx->new_code_size, state_reg, state, i);
    Instruction cmp_inst = CREATE_ABCk(OP_EQI, state_reg, int2sC(state), 0, 1);
    if (emitInstruction(ctx, cmp_inst) < 0) {
      luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
      return -1;
    }
    
    CFF_LOG("  [PC=%d] JMP -> 真实块%d (偏移量待定)", ctx->new_code_size, i);
    Instruction jmp_inst = CREATE_sJ(OP_JMP, 0, 0);
    int jmp_pc = emitInstruction(ctx, jmp_inst);
    if (jmp_pc < 0) {
      luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
      return -1;
    }
    all_block_jmp_pcs[i] = jmp_pc;

    /* 混淆：在此处插入 NOP 是安全的 */
    if (ctx->obfuscate_flags & OBFUSCATE_RANDOM_NOP) {
      int num_nops = 1 + (ctx->seed % 2);
      for (int k = 0; k < num_nops; k++) {
        NEXT_RAND(ctx->seed);
        emitInstruction(ctx, luaO_createNOP(ctx->seed));
      }
    }
  }
  
  /* 生成状态比较代码 - 虚假块 */
  if (num_bogus_blocks > 0) {
    CFF_LOG("--- 生成状态比较代码（虚假块）---");
    for (int i = 0; i < num_bogus_blocks; i++) {
      int state = ctx->num_blocks + i;
      
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
        state = luaO_encodeState(state, ctx->seed);
      }
      
      CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (虚假块%d)", 
              ctx->new_code_size, state_reg, state, i);
      Instruction cmp_inst = CREATE_ABCk(OP_EQI, state_reg, int2sC(state), 0, 1);
      if (emitInstruction(ctx, cmp_inst) < 0) {
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
        return -1;
      }
      
      CFF_LOG("  [PC=%d] JMP -> 虚假块%d (偏移量待定)", ctx->new_code_size, i);
      Instruction jmp_inst = CREATE_sJ(OP_JMP, 0, 0);
      int jmp_pc = emitInstruction(ctx, jmp_inst);
      if (jmp_pc < 0) {
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
        return -1;
      }
      all_block_jmp_pcs[ctx->num_blocks + i] = jmp_pc;

      /* 混淆：在此处插入 NOP 是安全的 */
      if (ctx->obfuscate_flags & OBFUSCATE_RANDOM_NOP) {
        int num_nops = 1 + (ctx->seed % 2);
        for (int k = 0; k < num_nops; k++) {
          NEXT_RAND(ctx->seed);
          emitInstruction(ctx, luaO_createNOP(ctx->seed));
        }
      }
    }
  }
  
  /* 如果启用函数交织，生成虚假函数入口检查 */
  int *fake_func_jmp_pcs = NULL;
  if (ctx->obfuscate_flags & OBFUSCATE_FUNC_INTERLEAVE) {
    fake_func_jmp_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * ctx->num_fake_funcs, 0);
    if (fake_func_jmp_pcs == NULL) {
      luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
      return -1;
    }
    
    CFF_LOG("--- 生成虚假函数入口检查 ---");
    unsigned int fake_seed = ctx->seed ^ 0xFEEDFACE;
    for (int f = 0; f < ctx->num_fake_funcs; f++) {
      if (emitFakeFunction(ctx, f, &fake_seed, &fake_func_jmp_pcs[f]) < 0) {
        luaM_free_(ctx->L, fake_func_jmp_pcs, sizeof(int) * ctx->num_fake_funcs);
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
        return -1;
      }
    }
  }
  
  /* 生成默认跳转回dispatcher的指令 */
  int dispatcher_end = ctx->new_code_size;
  Instruction loop_jmp = CREATE_sJ(OP_JMP, ctx->dispatcher_pc - dispatcher_end - 1 + OFFSET_sJ, 0);
  if (emitInstruction(ctx, loop_jmp) < 0) {
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    return -1;
  }
  
  /* 记录所有块的代码起始位置 */
  int *all_block_starts = (int *)luaM_malloc_(ctx->L, sizeof(int) * total_blocks, 0);
  if (all_block_starts == NULL) {
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    return -1;
  }
  
  /* 复制基本块并生成状态转移 */
  if (luaO_emitBlocksAndStubs(ctx, all_block_jmp_pcs, all_block_starts, num_bogus_blocks, &bogus_seed) != 0) {
    luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * total_blocks);
    luaM_free_(ctx->L, all_block_starts, sizeof(int) * total_blocks);
    return -1;
  }
  
  /* 修正dispatcher中的跳转偏移量 */
  CFF_LOG("--- 修正分发器跳转偏移 ---");
  for (int i = 0; i < ctx->num_blocks; i++) {
    int jmp_pc = all_block_jmp_pcs[i];
    int target_pc = all_block_starts[i];
    int offset = target_pc - jmp_pc - 1;
    
    CFF_LOG("  块%d: JMP@PC=%d -> 目标PC=%d, offset=%d", i, jmp_pc, target_pc, offset);
    SETARG_sJ(ctx->new_code[jmp_pc], offset);
  }
  
  /* 如果启用函数交织，生成虚假函数块并修正跳转 */
  if ((ctx->obfuscate_flags & OBFUSCATE_FUNC_INTERLEAVE) && fake_func_jmp_pcs != NULL) {
    CFF_LOG("--- 生成虚假函数块代码 ---");
    unsigned int fake_seed = ctx->seed ^ 0xFEEDFACE;
    for (int f = 0; f < ctx->num_fake_funcs; f++) {
      if (emitFakeFunctionBlocks(ctx, f, &fake_seed, fake_func_jmp_pcs[f]) < 0) {
        luaM_free_(ctx->L, fake_func_jmp_pcs, sizeof(int) * ctx->num_fake_funcs);
        luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * ctx->num_blocks);
        luaM_free_(ctx->L, all_block_starts, sizeof(int) * ctx->num_blocks);
        return -1;
      }
    }
    luaM_free_(ctx->L, fake_func_jmp_pcs, sizeof(int) * ctx->num_fake_funcs);
  }
  
  CFF_LOG("========== 扁平化代码生成完成，共 %d 条指令 ==========", ctx->new_code_size);
  
  luaM_free_(ctx->L, all_block_jmp_pcs, sizeof(int) * ctx->num_blocks);
  luaM_free_(ctx->L, all_block_starts, sizeof(int) * ctx->num_blocks);
  
  return 0;
}


/*
** =======================================================
** 公共API实现
** =======================================================
*/


/*
** 对函数原型进行控制流扁平化
** @param L Lua状态
** @param f 要处理的函数原型
** @param flags 混淆标志位组合
** @param seed 随机种子
** @param log_path 调试日志输出路径（NULL表示不输出日志）
** @return 成功返回0，失败返回错误码
*/
int luaO_flatten (lua_State *L, Proto *f, int flags, unsigned int seed,
                  const char *log_path) {
  /* 调试：输出 log_path 值 */
  fprintf(stderr, "[CFF DEBUG] luaO_flatten called, log_path=%s, flags=%d\n", 
          log_path ? log_path : "(null)", flags);
  fflush(stderr);
  
  /* 设置日志文件 */
  FILE *log_file = NULL;
  if (log_path != NULL) {
    fprintf(stderr, "[CFF DEBUG] Attempting to open log file: %s\n", log_path);
    fflush(stderr);
    log_file = fopen(log_path, "w");
    if (log_file != NULL) {
      fprintf(stderr, "[CFF DEBUG] Log file opened successfully\n");
      fflush(stderr);
      g_cff_log_file = log_file;
      CFF_LOG("======================================");
      CFF_LOG("CFF 控制流扁平化调试日志");
      CFF_LOG("======================================");
    } else {
      fprintf(stderr, "[CFF DEBUG] Failed to open log file!\n");
      fflush(stderr);
    }
  }
  
  /* 检查是否需要扁平化 */
  if (!(flags & OBFUSCATE_CFF)) {
    /* 未启用控制流扁平化，但可能需要VM保护 */
    if (flags & OBFUSCATE_VM_PROTECT) {
      CFF_LOG("跳过CFF，仅应用VM保护");
      int vm_result = luaO_vmProtect(L, f, seed ^ 0xFEDCBA98);
      if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
      return vm_result;
    }
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return 0;  /* 未启用任何混淆 */
  }
  
  /* 代码太短不值得扁平化 */
  if (f->sizecode < 4) {
    CFF_LOG("代码太短 (%d 条指令)，跳过扁平化", f->sizecode);
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return 0;
  }
  
  
  /* 初始化上下文 */
  CFFContext *ctx = initContext(L, f, flags, seed);
  if (ctx == NULL) {
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return -1;
  }
  
  /* 识别基本块 */
  if (luaO_identifyBlocks(ctx) != 0) {
    freeContext(ctx);
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return -1;
  }
  
  /* 基本块太少不值得扁平化 */
  if (ctx->num_blocks < 2) {
    CFF_LOG("基本块太少 (%d 个)，跳过扁平化", ctx->num_blocks);
    freeContext(ctx);
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return 0;
  }
  
  /* 如果启用了基本块打乱，进行打乱 */
  if (flags & OBFUSCATE_BLOCK_SHUFFLE) {
    CFF_LOG("启用基本块打乱");
    luaO_shuffleBlocks(ctx);
  }
  
  /* 生成扁平化代码 */
  int gen_result;
  if (flags & OBFUSCATE_BINARY_DISPATCHER) {
    CFF_LOG("使用二分查找分发器模式");
    gen_result = luaO_generateBinaryDispatcher(ctx);
  } else if (flags & OBFUSCATE_NESTED_DISPATCHER) {
    CFF_LOG("使用嵌套分发器模式");
    gen_result = luaO_generateNestedDispatcher(ctx);
  } else {
    CFF_LOG("使用标准分发器模式");
    gen_result = luaO_generateDispatcher(ctx);
  }
  
  if (gen_result != 0) {
    CFF_LOG("生成分发器失败！");
    freeContext(ctx);
    if (log_file != NULL) { fclose(log_file); g_cff_log_file = NULL; }
    return -1;
  }
  
  /* 更新函数原型 */
  /* 释放旧代码 */
  luaM_freearray(L, f->code, f->sizecode);
  
  /* 分配新代码 */
  f->code = luaM_newvectorchecked(L, ctx->new_code_size, Instruction);
  memcpy(f->code, ctx->new_code, sizeof(Instruction) * ctx->new_code_size);
  f->sizecode = ctx->new_code_size;
  
  /* 更新栈大小（增加状态寄存器） */
  int max_state_reg = ctx->state_reg;
  if (flags & OBFUSCATE_NESTED_DISPATCHER) {
    /* 嵌套模式需要两个状态寄存器 */
    if (ctx->outer_state_reg > max_state_reg) {
      max_state_reg = ctx->outer_state_reg;
    }
  }
  if (flags & OBFUSCATE_OPAQUE_PREDICATES) {
    /* 不透明谓词需要两个临时寄存器 */
    if (ctx->opaque_reg2 > max_state_reg) {
      max_state_reg = ctx->opaque_reg2;
    }
  }
  if (flags & OBFUSCATE_FUNC_INTERLEAVE) {
    /* 函数交织需要函数ID寄存器 */
    if (ctx->func_id_reg > max_state_reg) {
      max_state_reg = ctx->func_id_reg;
    }
  }
  if (max_state_reg >= f->maxstacksize) {
    f->maxstacksize = max_state_reg + 1;
  }
  
  /* 在difierline_mode中标记已扁平化 */
  f->difierline_mode |= OBFUSCATE_CFF;
  if (flags & OBFUSCATE_NESTED_DISPATCHER) {
    f->difierline_mode |= OBFUSCATE_NESTED_DISPATCHER;
  }
  if (flags & OBFUSCATE_OPAQUE_PREDICATES) {
    f->difierline_mode |= OBFUSCATE_OPAQUE_PREDICATES;
  }
  if (flags & OBFUSCATE_FUNC_INTERLEAVE) {
    f->difierline_mode |= OBFUSCATE_FUNC_INTERLEAVE;
  }
  f->difierline_magicnum = CFF_MAGIC;
  f->difierline_data = ((uint64_t)ctx->num_blocks << 32) | ctx->seed;
  
  CFF_LOG("扁平化完成！新代码大小: %d 条指令", ctx->new_code_size);
  
  freeContext(ctx);
  
  /* 如果启用VM保护，在扁平化之后应用 */
  if (flags & OBFUSCATE_VM_PROTECT) {
    CFF_LOG("应用VM保护...");
    if (luaO_vmProtect(L, f, seed ^ 0xFEDCBA98) != 0) {
      CFF_LOG("VM保护失败！");
      if (log_file != NULL) { 
        fclose(log_file); 
        g_cff_log_file = NULL; 
      }
      return -1;
    }
  }
  
  /* 关闭日志文件 */
  if (log_file != NULL) { 
    fclose(log_file); 
    g_cff_log_file = NULL; 
  }
  
  return 0;
}


/*
** 对函数原型进行反扁平化
** @param L Lua状态
** @param f 要处理的函数原型
** @param metadata 扁平化元数据（如果为NULL则从f中读取）
** @return 成功返回0，失败返回错误码
**
** 注意：反扁平化在运行时不是必须的，因为扁平化后的代码
** 仍然是有效的Lua字节码，可以直接执行。
** 这个函数主要用于调试或需要恢复原始控制流的场景。
*/
int luaO_unflatten (lua_State *L, Proto *f, CFFMetadata *metadata) {
  if (!(f->difierline_mode & OBFUSCATE_CFF)) return 0;
  CFF_LOG("========== 开始反扁平化 ==========");
  if (metadata == NULL) {
    if (f->difierline_magicnum != CFF_MAGIC) {
      CFF_LOG("反扁平化失败：无效的魔数");
      return -1;
    }
    f->difierline_mode &= ~OBFUSCATE_CFF;
    f->difierline_mode &= ~OBFUSCATE_NESTED_DISPATCHER;
    f->difierline_mode &= ~OBFUSCATE_OPAQUE_PREDICATES;
    f->difierline_mode &= ~OBFUSCATE_FUNC_INTERLEAVE;
    CFF_LOG("已清除扁平化标志（仅标记清除）");
    return 0;
  }
  if (metadata->enabled) {
    CFF_LOG("使用元数据进行反扁平化: num_blocks=%d", metadata->num_blocks);
    f->difierline_mode &= ~OBFUSCATE_CFF;
  }
  (void)L;
  return 0;
}


/*
** 序列化扁平化元数据
** @param L Lua状态
** @param ctx 扁平化上下文
** @param buffer 输出缓冲区
** @param size 缓冲区大小（输入输出参数）
** @return 成功返回0，失败返回错误码
*/
int luaO_serializeMetadata (lua_State *L, CFFContext *ctx, 
                             void *buffer, size_t *size) {
  /* 计算所需大小 */
  size_t needed = sizeof(int) * 4 +  /* magic, version, num_blocks, state_reg */
                  sizeof(unsigned int) +  /* seed */
                  sizeof(BasicBlock) * ctx->num_blocks;
  
  if (buffer == NULL) {
    *size = needed;
    return 0;
  }
  
  if (*size < needed) {
    *size = needed;
    return -1;  /* 缓冲区太小 */
  }
  
  unsigned char *ptr = (unsigned char *)buffer;
  
  /* 写入魔数 */
  *(int *)ptr = CFF_MAGIC;
  ptr += sizeof(int);
  
  /* 写入版本 */
  *(int *)ptr = CFF_VERSION;
  ptr += sizeof(int);
  
  /* 写入基本块数量 */
  *(int *)ptr = ctx->num_blocks;
  ptr += sizeof(int);
  
  /* 写入状态寄存器 */
  *(int *)ptr = ctx->state_reg;
  ptr += sizeof(int);
  
  /* 写入种子 */
  *(unsigned int *)ptr = ctx->seed;
  ptr += sizeof(unsigned int);
  
  /* 写入基本块信息 */
  memcpy(ptr, ctx->blocks, sizeof(BasicBlock) * ctx->num_blocks);
  
  *size = needed;
  
  (void)L;
  return 0;
}


/*
** 反序列化扁平化元数据
** @param L Lua状态
** @param buffer 输入缓冲区
** @param size 缓冲区大小
** @param metadata 输出元数据结构
** @return 成功返回0，失败返回错误码
*/
int luaO_deserializeMetadata (lua_State *L, const void *buffer, 
                               size_t size, CFFMetadata *metadata) {
  if (size < sizeof(int) * 4 + sizeof(unsigned int)) {
    return -1;  /* 数据太短 */
  }
  
  const unsigned char *ptr = (const unsigned char *)buffer;
  
  /* 读取并验证魔数 */
  int magic = *(const int *)ptr;
  if (magic != CFF_MAGIC) {
    return -1;  /* 无效魔数 */
  }
  ptr += sizeof(int);
  
  /* 读取并验证版本 */
  int version = *(const int *)ptr;
  if (version != CFF_VERSION) {
    return -1;  /* 不支持的版本 */
  }
  ptr += sizeof(int);
  
  /* 读取基本块数量 */
  metadata->num_blocks = *(const int *)ptr;
  ptr += sizeof(int);
  
  /* 读取状态寄存器 */
  metadata->state_reg = *(const int *)ptr;
  ptr += sizeof(int);
  
  /* 读取种子 */
  metadata->seed = *(const unsigned int *)ptr;
  ptr += sizeof(unsigned int);
  
  /* 验证数据大小 */
  size_t expected_size = sizeof(int) * 4 + sizeof(unsigned int) +
                         sizeof(BasicBlock) * metadata->num_blocks;
  if (size < expected_size) {
    return -1;  /* 数据不完整 */
  }
  
  /* 分配并读取块映射 */
  metadata->block_mapping = (int *)luaM_malloc_(L, sizeof(int) * metadata->num_blocks, 0);
  if (metadata->block_mapping == NULL) {
    return -1;
  }
  
  /* 从BasicBlock数据提取映射 */
  const BasicBlock *blocks = (const BasicBlock *)ptr;
  for (int i = 0; i < metadata->num_blocks; i++) {
    metadata->block_mapping[i] = blocks[i].start_pc;
  }
  
  metadata->enabled = 1;
  
  return 0;
}


/*
** 释放扁平化元数据占用的内存
** @param L Lua状态
** @param metadata 要释放的元数据
*/
void luaO_freeMetadata (lua_State *L, CFFMetadata *metadata) {
  if (metadata == NULL) return;
  
  if (metadata->block_mapping != NULL) {
    luaM_free_(L, metadata->block_mapping, sizeof(int) * metadata->num_blocks);
    metadata->block_mapping = NULL;
  }
  
  metadata->enabled = 0;
}


/*
** =======================================================
** 嵌套分发器生成
** =======================================================
*/


/* 每个分组的最大基本块数量 */
#define NESTED_GROUP_SIZE  4


/*
** 将基本块分配到不同的分组中
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回-1
**
** 功能描述：
** 将基本块分成多个分组，每个分组由外层分发器的一个状态对应。
** 分组内的基本块由内层分发器管理。
*/
static int partitionBlocksIntoGroups (CFFContext *ctx) {
  if (ctx->num_blocks == 0) return 0;
  
  /* 计算分组数量 */
  ctx->num_groups = (ctx->num_blocks + NESTED_GROUP_SIZE - 1) / NESTED_GROUP_SIZE;
  if (ctx->num_groups < 2) ctx->num_groups = 2;  /* 至少2个分组才有意义 */
  
  /* 分配分组起始索引数组（多分配一个作为结束标记） */
  ctx->group_starts = (int *)luaM_malloc_(ctx->L, sizeof(int) * (ctx->num_groups + 1), 0);
  if (ctx->group_starts == NULL) return -1;
  
  /* 均匀分配块到各个分组 */
  int blocks_per_group = (ctx->num_blocks + ctx->num_groups - 1) / ctx->num_groups;
  for (int g = 0; g < ctx->num_groups; g++) {
    ctx->group_starts[g] = g * blocks_per_group;
    if (ctx->group_starts[g] > ctx->num_blocks) {
      ctx->group_starts[g] = ctx->num_blocks;
    }
  }
  ctx->group_starts[ctx->num_groups] = ctx->num_blocks;  /* 结束标记 */
  
  CFF_LOG("基本块分组: %d 个块分成 %d 个分组", ctx->num_blocks, ctx->num_groups);
  for (int g = 0; g < ctx->num_groups; g++) {
    CFF_LOG("  分组 %d: 块 [%d, %d)", g, ctx->group_starts[g], ctx->group_starts[g+1]);
  }
  
  return 0;
}


/*
** 查找基本块所属的分组
** @param ctx 上下文
** @param block_idx 基本块索引
** @return 分组索引
*/
static int findBlockGroup (CFFContext *ctx, int block_idx) {
  for (int g = 0; g < ctx->num_groups; g++) {
    if (block_idx >= ctx->group_starts[g] && block_idx < ctx->group_starts[g+1]) {
      return g;
    }
  }
  return 0;  /* 默认返回第一个分组 */
}


/*
** 生成嵌套分发器代码
** @param ctx 扁平化上下文
** @return 成功返回0，失败返回错误码
**
** 嵌套分发器结构：
**   LOADI outer_state_reg, initial_outer_state
**   LOADI state_reg, initial_inner_state
** outer_dispatcher:
**   EQI outer_state_reg, 0, k=1
**   JMP inner_dispatcher_0
**   EQI outer_state_reg, 1, k=1
**   JMP inner_dispatcher_1
**   ...
**   JMP outer_dispatcher  ; 默认循环
** 
** inner_dispatcher_0:
**   EQI state_reg, state_0, k=1
**   JMP block_0
**   EQI state_reg, state_1, k=1
**   JMP block_1
**   ...
**   JMP outer_dispatcher  ; 返回外层
**
** block_N:
**   ; 原始代码
**   LOADI outer_state_reg, next_outer
**   LOADI state_reg, next_inner
**   JMP outer_dispatcher
*/
int luaO_generateNestedDispatcher (CFFContext *ctx) {
  if (ctx->num_blocks == 0) return 0;
  
  int state_reg = ctx->state_reg;
  int outer_state_reg = ctx->outer_state_reg;
  unsigned int bogus_seed = ctx->seed;
  Proto *f = ctx->f;
  
  CFF_LOG("========== 开始生成嵌套分发器代码 ==========");
  CFF_LOG("内层状态寄存器: R[%d]", state_reg);
  CFF_LOG("外层状态寄存器: R[%d]", outer_state_reg);
  
  /* 分配基本块到分组 */
  if (partitionBlocksIntoGroups(ctx) != 0) return -1;
  
  /* 找到入口块并确定初始状态 */
  int entry_block = 0;
  for (int i = 0; i < ctx->num_blocks; i++) {
    if (ctx->blocks[i].is_entry) {
      entry_block = i;
      break;
    }
  }
  
  int entry_group = findBlockGroup(ctx, entry_block);
  int entry_inner_state = ctx->blocks[entry_block].state_id;
  
  CFF_LOG("入口块: 块%d, 分组%d, 内层状态=%d", entry_block, entry_group, entry_inner_state);
  
  /* 编码初始状态 */
  int initial_outer = entry_group;
  int initial_inner = entry_inner_state;
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
    initial_outer = luaO_encodeState(entry_group, ctx->seed);
    initial_inner = luaO_encodeState(entry_inner_state, ctx->seed ^ 0x12345678);
  }
  
  /* 生成初始化指令 */
  CFF_LOG("生成初始化: LOADI R[%d], %d (外层)", outer_state_reg, initial_outer);
  Instruction init_outer = CREATE_ABx(OP_LOADI, outer_state_reg, initial_outer + OFFSET_sBx);
  if (emitInstruction(ctx, init_outer) < 0) return -1;
  
  CFF_LOG("生成初始化: LOADI R[%d], %d (内层)", state_reg, initial_inner);
  Instruction init_inner = CREATE_ABx(OP_LOADI, state_reg, initial_inner + OFFSET_sBx);
  if (emitInstruction(ctx, init_inner) < 0) return -1;
  
  /* 记录外层分发器位置 */
  ctx->outer_dispatcher_pc = ctx->new_code_size;
  CFF_LOG("外层分发器起始位置: PC=%d", ctx->outer_dispatcher_pc);
  
  /* 为每个分组分配跳转PC */
  int *group_jmp_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * ctx->num_groups, 0);
  int *inner_dispatcher_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * ctx->num_groups, 0);
  if (group_jmp_pcs == NULL || inner_dispatcher_pcs == NULL) {
    if (group_jmp_pcs) luaM_free_(ctx->L, group_jmp_pcs, sizeof(int) * ctx->num_groups);
    if (inner_dispatcher_pcs) luaM_free_(ctx->L, inner_dispatcher_pcs, sizeof(int) * ctx->num_groups);
    return -1;
  }
  
  /* 生成外层分发器的状态比较 */
  CFF_LOG("--- 生成外层分发器状态比较 ---");
  for (int g = 0; g < ctx->num_groups; g++) {
    int outer_state = g;
    if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
      outer_state = luaO_encodeState(g, ctx->seed);
    }
    
    CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (分组%d)", 
            ctx->new_code_size, outer_state_reg, outer_state, g);
    Instruction cmp_inst = CREATE_ABCk(OP_EQI, outer_state_reg, int2sC(outer_state), 0, 1);
    if (emitInstruction(ctx, cmp_inst) < 0) goto cleanup_nested;
    
    CFF_LOG("  [PC=%d] JMP -> 内层分发器%d (偏移量待定)", ctx->new_code_size, g);
    Instruction jmp_inst = CREATE_sJ(OP_JMP, 0, 0);
    group_jmp_pcs[g] = emitInstruction(ctx, jmp_inst);
    if (group_jmp_pcs[g] < 0) goto cleanup_nested;
  }
  
  /* 外层分发器的默认跳转（循环回自己） */
  int outer_loop_jmp_pc = ctx->new_code_size;
  Instruction outer_loop = CREATE_sJ(OP_JMP, ctx->outer_dispatcher_pc - outer_loop_jmp_pc - 1 + OFFSET_sJ, 0);
  if (emitInstruction(ctx, outer_loop) < 0) goto cleanup_nested;
  
  /* 为每个分组生成内层分发器 */
  CFF_LOG("--- 生成内层分发器 ---");
  int *block_jmp_pcs = (int *)luaM_malloc_(ctx->L, sizeof(int) * ctx->num_blocks, 0);
  int *block_starts = (int *)luaM_malloc_(ctx->L, sizeof(int) * ctx->num_blocks, 0);
  if (block_jmp_pcs == NULL || block_starts == NULL) {
    if (block_jmp_pcs) luaM_free_(ctx->L, block_jmp_pcs, sizeof(int) * ctx->num_blocks);
    if (block_starts) luaM_free_(ctx->L, block_starts, sizeof(int) * ctx->num_blocks);
    goto cleanup_nested;
  }
  
  for (int g = 0; g < ctx->num_groups; g++) {
    inner_dispatcher_pcs[g] = ctx->new_code_size;
    CFF_LOG("内层分发器 %d 起始位置: PC=%d", g, inner_dispatcher_pcs[g]);
    
    /* 修正外层分发器到此内层分发器的跳转 */
    int offset = inner_dispatcher_pcs[g] - group_jmp_pcs[g] - 1;
    SETARG_sJ(ctx->new_code[group_jmp_pcs[g]], offset);
    
    int group_start = ctx->group_starts[g];
    int group_end = ctx->group_starts[g + 1];
    
    /* 生成此分组内所有块的状态比较 */
    for (int i = group_start; i < group_end; i++) {
      int inner_state = ctx->blocks[i].state_id;
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
        inner_state = luaO_encodeState(inner_state, ctx->seed ^ 0x12345678);
      }
      
      CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (块%d)", 
              ctx->new_code_size, state_reg, inner_state, i);
      Instruction cmp_inst = CREATE_ABCk(OP_EQI, state_reg, int2sC(inner_state), 0, 1);
      if (emitInstruction(ctx, cmp_inst) < 0) goto cleanup_all;
      
      CFF_LOG("  [PC=%d] JMP -> 块%d (偏移量待定)", ctx->new_code_size, i);
      Instruction jmp_inst = CREATE_sJ(OP_JMP, 0, 0);
      block_jmp_pcs[i] = emitInstruction(ctx, jmp_inst);
      if (block_jmp_pcs[i] < 0) goto cleanup_all;
    }
    
    /* 内层分发器的默认跳转：跳回外层分发器 */
    int inner_default_jmp_pc = ctx->new_code_size;
    Instruction inner_default = CREATE_sJ(OP_JMP, ctx->outer_dispatcher_pc - inner_default_jmp_pc - 1 + OFFSET_sJ, 0);
    if (emitInstruction(ctx, inner_default) < 0) goto cleanup_all;
  }
  
  /* 复制基本块代码 */
  CFF_LOG("--- 复制基本块代码 ---");
  for (int i = 0; i < ctx->num_blocks; i++) {
    BasicBlock *block = &ctx->blocks[i];
    block_starts[i] = ctx->new_code_size;
    
    CFF_LOG("块 %d: 原始PC [%d, %d), 新起始PC=%d", 
            i, block->start_pc, block->end_pc, block_starts[i]);
    
    /* 修正跳转到此块的指令 */
    int offset = block_starts[i] - block_jmp_pcs[i] - 1;
    SETARG_sJ(ctx->new_code[block_jmp_pcs[i]], offset);
    
    /* 分析基本块的最后指令 */
    int last_pc = block->end_pc - 1;
    OpCode last_op = OP_NOP;
    int has_cond_test = 0;
    int cond_test_pc = -1;
    
    if (last_pc >= block->start_pc) {
      last_op = GET_OPCODE(f->code[last_pc]);
      
      /* 检查条件测试+JMP模式 */
      if (last_op == OP_JMP && last_pc > block->start_pc) {
        OpCode prev_op = GET_OPCODE(f->code[last_pc - 1]);
        if (isConditionalTest(prev_op)) {
          has_cond_test = 1;
          cond_test_pc = last_pc - 1;
        }
      }
    }
    
    /* 确定复制范围 */
    int copy_end = block->end_pc;
    if (has_cond_test) {
      copy_end = cond_test_pc;
    } else if (last_op == OP_JMP) {
      copy_end = block->end_pc - 1;
    }
    
    /* 复制指令 */
    for (int pc = block->start_pc; pc < copy_end; pc++) {
      if (emitInstruction(ctx, f->code[pc]) < 0) goto cleanup_all;
    }
    
    /* 处理基本块出口 */
    if (block->is_exit) {
      /* 出口块：复制返回指令 */
      if (copy_end < block->end_pc) {
        for (int pc = copy_end; pc < block->end_pc; pc++) {
          if (emitInstruction(ctx, f->code[pc]) < 0) goto cleanup_all;
        }
      }
    } else if (has_cond_test) {
      /* 条件分支块：生成双路状态转换 */
      Instruction cond_inst = f->code[cond_test_pc];
      if (emitInstruction(ctx, cond_inst) < 0) goto cleanup_all;
      
      /* 获取分支目标 */
      Instruction orig_jmp = f->code[last_pc];
      int orig_jmp_target = luaO_getJumpTarget(orig_jmp, last_pc);
      int else_block = findBlockStartingAt(ctx, orig_jmp_target);
      int then_block = findBlockStartingAt(ctx, last_pc + 1);
      if (then_block < 0) then_block = block->fall_through;
      
      /* 计算then/else分支的分组和状态 */
      int then_group = then_block >= 0 ? findBlockGroup(ctx, then_block) : 0;
      int else_group = else_block >= 0 ? findBlockGroup(ctx, else_block) : 0;
      int then_inner = then_block >= 0 ? ctx->blocks[then_block].state_id : 0;
      int else_inner = else_block >= 0 ? ctx->blocks[else_block].state_id : 0;
      
      if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
        then_group = luaO_encodeState(then_group, ctx->seed);
        else_group = luaO_encodeState(else_group, ctx->seed);
        then_inner = luaO_encodeState(then_inner, ctx->seed ^ 0x12345678);
        else_inner = luaO_encodeState(else_inner, ctx->seed ^ 0x12345678);
      }
      
      /* 生成: JMP +3 (跳过then分支的3条状态设置指令) */
      Instruction skip_then = CREATE_sJ(OP_JMP, 3 + OFFSET_sJ, 0);
      if (emitInstruction(ctx, skip_then) < 0) goto cleanup_all;
      
      /* then分支状态设置 */
      Instruction then_outer_inst = CREATE_ABx(OP_LOADI, outer_state_reg, then_group + OFFSET_sBx);
      if (emitInstruction(ctx, then_outer_inst) < 0) goto cleanup_all;
      Instruction then_inner_inst = CREATE_ABx(OP_LOADI, state_reg, then_inner + OFFSET_sBx);
      if (emitInstruction(ctx, then_inner_inst) < 0) goto cleanup_all;
      
      int jmp_offset1 = ctx->outer_dispatcher_pc - ctx->new_code_size - 1;
      Instruction jmp_disp1 = CREATE_sJ(OP_JMP, jmp_offset1 + OFFSET_sJ, 0);
      if (emitInstruction(ctx, jmp_disp1) < 0) goto cleanup_all;
      
      /* else分支状态设置 */
      Instruction else_outer_inst = CREATE_ABx(OP_LOADI, outer_state_reg, else_group + OFFSET_sBx);
      if (emitInstruction(ctx, else_outer_inst) < 0) goto cleanup_all;
      Instruction else_inner_inst = CREATE_ABx(OP_LOADI, state_reg, else_inner + OFFSET_sBx);
      if (emitInstruction(ctx, else_inner_inst) < 0) goto cleanup_all;
      
      int jmp_offset2 = ctx->outer_dispatcher_pc - ctx->new_code_size - 1;
      Instruction jmp_disp2 = CREATE_sJ(OP_JMP, jmp_offset2 + OFFSET_sJ, 0);
      if (emitInstruction(ctx, jmp_disp2) < 0) goto cleanup_all;
      
    } else {
      /* 普通块：计算下一个状态 */
      int next_block = block->original_target >= 0 ? block->original_target : block->fall_through;
      
      if (next_block >= 0) {
        int next_group = findBlockGroup(ctx, next_block);
        int next_inner = ctx->blocks[next_block].state_id;
        
        if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
          next_group = luaO_encodeState(next_group, ctx->seed);
          next_inner = luaO_encodeState(next_inner, ctx->seed ^ 0x12345678);
        }
        
        /* 设置外层和内层状态 */
        Instruction outer_inst = CREATE_ABx(OP_LOADI, outer_state_reg, next_group + OFFSET_sBx);
        if (emitInstruction(ctx, outer_inst) < 0) goto cleanup_all;
        
        Instruction inner_inst = CREATE_ABx(OP_LOADI, state_reg, next_inner + OFFSET_sBx);
        if (emitInstruction(ctx, inner_inst) < 0) goto cleanup_all;
        
        /* 跳转到外层分发器 */
        int jmp_offset = ctx->outer_dispatcher_pc - ctx->new_code_size - 1;
        Instruction jmp_inst = CREATE_sJ(OP_JMP, jmp_offset + OFFSET_sJ, 0);
        if (emitInstruction(ctx, jmp_inst) < 0) goto cleanup_all;
      }
    }
  }
  
  CFF_LOG("========== 嵌套分发器生成完成，共 %d 条指令 ==========", ctx->new_code_size);
  
  luaM_free_(ctx->L, block_jmp_pcs, sizeof(int) * ctx->num_blocks);
  luaM_free_(ctx->L, block_starts, sizeof(int) * ctx->num_blocks);
  luaM_free_(ctx->L, group_jmp_pcs, sizeof(int) * ctx->num_groups);
  luaM_free_(ctx->L, inner_dispatcher_pcs, sizeof(int) * ctx->num_groups);
  return 0;

cleanup_all:
  luaM_free_(ctx->L, block_jmp_pcs, sizeof(int) * ctx->num_blocks);
  luaM_free_(ctx->L, block_starts, sizeof(int) * ctx->num_blocks);
cleanup_nested:
  luaM_free_(ctx->L, group_jmp_pcs, sizeof(int) * ctx->num_groups);
  luaM_free_(ctx->L, inner_dispatcher_pcs, sizeof(int) * ctx->num_groups);
  return -1;
}


/*
** =======================================================
** NOP指令功能实现
** =======================================================
*/


/*
** 生成带虚假参数的NOP指令
** @param seed 随机种子（用于生成随机参数）
** @return 生成的NOP指令
**
** 功能描述：
** 创建一个OP_NOP指令，其A/B/C参数被填充为随机值，
** 这些参数在执行时被忽略，但可以干扰反编译器的分析。
*/
Instruction luaO_createNOP (unsigned int seed) {
  /* 生成随机参数值 */
  unsigned int r = seed;
  NEXT_RAND(r);
  int fake_a = (r >> 16) % 256;   /* 0-255 范围的虚假A参数 */
  NEXT_RAND(r);
  int fake_b = (r >> 16) % 256;   /* 0-255 范围的虚假B参数 */
  NEXT_RAND(r);
  int fake_c = (r >> 16) % 256;   /* 0-255 范围的虚假C参数 */
  
  /* 创建NOP指令：OP_NOP A B C k=0 */
  Instruction nop = CREATE_ABCk(OP_NOP, fake_a, fake_b, fake_c, 0);
  return nop;
}


/*
** =======================================================
** 不透明谓词功能实现
** =======================================================
*/


/*
** 不透明谓词变体数量
*/
#define NUM_OPAQUE_VARIANTS  10


/*
** 生成恒真不透明谓词
** @param ctx 上下文
** @param seed 随机种子指针
** @return 成功返回0，失败返回-1
**
** 恒真谓词示例：
** 1. x*x >= 0          (平方数非负)
** 2. x + 0 == x        (加0恒等)
** 3. 2*x - x == x      (乘2减x恒等)
** 4. x - x == 0        (自减恒为0)
** 5. (x | 1) != 0      (任何数或1都不为0)
** 6. (x ^ x) == 0      (自异或恒为0)
** 7. (x & 0) == 0
** 8. (x | 0) == x
** 9. (x & x) == x
** 10. (x | -1) == -1
**
** 生成的字节码结构：
**   LOADI reg1, random_value      ; 加载一个随机值
**   MUL reg2, reg1, reg1          ; reg2 = reg1 * reg1
**   GEI reg2, 0, k=0              ; 检查 reg2 >= 0 (恒真)
**   JMP +1                        ; 条件为假时跳过（永不执行）
**   ; 继续正常执行...
*/
static int emitAlwaysTruePredicate (CFFContext *ctx, unsigned int *seed) {
  int reg1 = ctx->opaque_reg1;
  int reg2 = ctx->opaque_reg2;
  
  NEXT_RAND(*seed);
  int variant = *seed % NUM_OPAQUE_VARIANTS;
  
  NEXT_RAND(*seed);
  int random_val = (*seed % 1000) - 500;  /* -500 ~ 499 */
  
  CFF_LOG("  生成恒真谓词: 变体%d, 随机值=%d", variant, random_val);
  
  switch (variant) {
    case 0: {
      /* x*x >= 0 (平方数非负) */
      /* LOADI reg1, random_val */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* MUL reg2, reg1, reg1 */
      Instruction mul = CREATE_ABCk(OP_MUL, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, mul) < 0) return -1;
      
      /* 注意：不使用 MMBIN，因为它会干扰 VM 执行流程 */
      
      /* GEI reg2, 0, k=0 (reg2 >= 0 ? skip next) */
      Instruction cmp = CREATE_ABCk(OP_GEI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 1: {
      /* x + 0 == x (恒等式，加0不变) */
      /* LOADI reg1, random_val */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* ADDI reg2, reg1, 0 (reg2 = reg1 + 0 = reg1) */
      Instruction addi = CREATE_ABCk(OP_ADDI, reg2, reg1, int2sC(0), 0);
      if (emitInstruction(ctx, addi) < 0) return -1;
      
      /* EQ reg2, reg1, k=0 (reg2 == reg1 ? 恒真) */
      Instruction cmp = CREATE_ABCk(OP_EQ, reg2, reg1, 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 2: {
      /* 2*x - x == x (恒等式) */
      /* LOADI reg1, random_val */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* SHLI reg2, reg1, 1 (reg2 = reg1 * 2) */
      Instruction shl = CREATE_ABCk(OP_SHLI, reg2, reg1, int2sC(1), 0);
      if (emitInstruction(ctx, shl) < 0) return -1;
      
      /* SUB reg2, reg2, reg1 (reg2 = 2*x - x = x) */
      Instruction sub = CREATE_ABCk(OP_SUB, reg2, reg2, reg1, 0);
      if (emitInstruction(ctx, sub) < 0) return -1;
      
      /* EQ reg2, reg1, k=0 (reg2 == reg1 ? 恒真) */
      Instruction cmp = CREATE_ABCk(OP_EQ, reg2, reg1, 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 3: {
      /* x - x == 0 (恒等式) */
      /* LOADI reg1, random_val */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* SUB reg2, reg1, reg1 (reg2 = x - x = 0) */
      Instruction sub = CREATE_ABCk(OP_SUB, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, sub) < 0) return -1;
      
      /* EQI reg2, 0, k=0 (reg2 == 0 ? 恒真) */
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 4: {
      /* (x | 1) != 0 (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* BORI reg2, reg1, 1 */
      Instruction bor = CREATE_ABCk(OP_BORK, reg2, reg1, int2sC(1), 0);
      /* Wait, OP_BORK is BORK A B C where K[C] is integer. 
      ** We should use LOADI for 1 and then BOR.
      */
      Instruction load1 = CREATE_ABx(OP_LOADI, reg2, 1 + OFFSET_sBx);
      if (emitInstruction(ctx, load1) < 0) return -1;
      Instruction bor_inst = CREATE_ABCk(OP_BOR, reg2, reg1, reg2, 0);
      if (emitInstruction(ctx, bor_inst) < 0) return -1;
      
      /* EQI reg2, 0, k=1 (reg2 != 0 ? 恒真) */
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 1);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 5: {
      /* (x ^ x) == 0 (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      Instruction bxor = CREATE_ABCk(OP_BXOR, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, bxor) < 0) return -1;
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    case 6: {
      /* (x & 0) == 0 (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      Instruction load0 = CREATE_ABx(OP_LOADI, reg2, 0 + OFFSET_sBx);
      if (emitInstruction(ctx, load0) < 0) return -1;
      Instruction band = CREATE_ABCk(OP_BAND, reg2, reg1, reg2, 0);
      if (emitInstruction(ctx, band) < 0) return -1;
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    case 7: {
      /* (x | 0) == x (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      Instruction load0 = CREATE_ABx(OP_LOADI, reg2, 0 + OFFSET_sBx);
      if (emitInstruction(ctx, load0) < 0) return -1;
      Instruction bor = CREATE_ABCk(OP_BOR, reg2, reg1, reg2, 0);
      if (emitInstruction(ctx, bor) < 0) return -1;
      Instruction cmp = CREATE_ABCk(OP_EQ, reg2, reg1, 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    case 8: {
      /* (x & x) == x (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      Instruction band = CREATE_ABCk(OP_BAND, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, band) < 0) return -1;
      Instruction cmp = CREATE_ABCk(OP_EQ, reg2, reg1, 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    default: {
      /* (x | -1) == -1 (恒真) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      Instruction loadm1 = CREATE_ABx(OP_LOADI, reg2, -1 + OFFSET_sBx);
      if (emitInstruction(ctx, loadm1) < 0) return -1;
      Instruction bor = CREATE_ABCk(OP_BOR, reg2, reg1, reg2, 0);
      if (emitInstruction(ctx, bor) < 0) return -1;
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(-1), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
  }
  
  return 0;
}


/*
** 生成恒假不透明谓词
** @param ctx 上下文
** @param seed 随机种子指针
** @return 成功返回0，失败返回-1
**
** 恒假谓词示例：
** 1. x*x < 0           (平方数不可能为负)
** 2. x != x            (自身不等于自身，假)
** 3. x - x != 0        (x减x必为0)
*/
static int emitAlwaysFalsePredicate (CFFContext *ctx, unsigned int *seed) {
  int reg1 = ctx->opaque_reg1;
  int reg2 = ctx->opaque_reg2;
  
  NEXT_RAND(*seed);
  int variant = *seed % 5;
  
  NEXT_RAND(*seed);
  int random_val = (*seed % 1000) - 500;
  
  CFF_LOG("  生成恒假谓词: 变体%d, 随机值=%d", variant, random_val);
  
  switch (variant) {
    case 0: {
      /* x*x < 0 (平方数不可能为负) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      Instruction mul = CREATE_ABCk(OP_MUL, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, mul) < 0) return -1;
      
      /* 注意：不使用 MMBIN，因为它会干扰 VM 执行流程 */
      
      /* LTI reg2, 0, k=0 (reg2 < 0 ? 恒假) */
      Instruction cmp = CREATE_ABCk(OP_LTI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 1: {
      /* x - x != 0 (恒假) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      Instruction sub = CREATE_ABCk(OP_SUB, reg2, reg1, reg1, 0);
      if (emitInstruction(ctx, sub) < 0) return -1;
      
      /* EQI reg2, 0, k=1 (reg2 != 0 ? 恒假，因为k=1表示不等) */
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 1);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 2: {
      /* x + 1 == x (恒假，除非溢出) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      Instruction addi = CREATE_ABCk(OP_ADDI, reg2, reg1, int2sC(1), 0);
      if (emitInstruction(ctx, addi) < 0) return -1;
      
      /* EQ reg2, reg1, k=0 (reg2 == reg1 ? 恒假) */
      Instruction cmp = CREATE_ABCk(OP_EQ, reg2, reg1, 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    case 3: {
      /* x != x (恒假) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      /* EQ reg1, reg1, k=1 (reg1 != reg1 ?) */
      Instruction cmp = CREATE_ABCk(OP_EQ, reg1, reg1, 0, 1);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
    
    default: {
      /* (x | 1) == 0 (恒假) */
      Instruction load = CREATE_ABx(OP_LOADI, reg1, random_val + OFFSET_sBx);
      if (emitInstruction(ctx, load) < 0) return -1;
      
      Instruction load1 = CREATE_ABx(OP_LOADI, reg2, 1 + OFFSET_sBx);
      if (emitInstruction(ctx, load1) < 0) return -1;
      Instruction bor_inst = CREATE_ABCk(OP_BOR, reg2, reg1, reg2, 0);
      if (emitInstruction(ctx, bor_inst) < 0) return -1;
      
      /* EQI reg2, 0, k=0 (reg2 == 0 ? 恒假) */
      Instruction cmp = CREATE_ABCk(OP_EQI, reg2, int2sC(0), 0, 0);
      if (emitInstruction(ctx, cmp) < 0) return -1;
      break;
    }
  }
  
  return 0;
}


/*
** 生成不透明谓词代码
** @param ctx 扁平化上下文
** @param type 谓词类型（恒真或恒假）
** @param seed 随机种子指针
** @return 成功返回生成的指令数，失败返回-1
*/
int luaO_emitOpaquePredicate (CFFContext *ctx, OpaquePredicateType type,
                               unsigned int *seed) {
  int start_size = ctx->new_code_size;
  int result;
  
  if (type == OP_ALWAYS_TRUE) {
    result = emitAlwaysTruePredicate(ctx, seed);
  } else {
    result = emitAlwaysFalsePredicate(ctx, seed);
  }
  
  if (result < 0) return -1;
  
  return ctx->new_code_size - start_size;
}


/*
** =======================================================
** 函数交织功能实现
** =======================================================
*/


/*
** 虚假函数模板
** 用于生成看起来像真实函数的代码序列
*/
typedef enum {
  FAKE_FUNC_CALCULATOR,   /* 模拟计算函数 */
  FAKE_FUNC_STRING_OP,    /* 模拟字符串操作 */
  FAKE_FUNC_TABLE_OP,     /* 模拟表操作 */
  FAKE_FUNC_LOOP          /* 模拟循环函数 */
} FakeFuncType;


/*
** 生成虚假函数的一个基本块
** @param ctx 上下文
** @param func_type 虚假函数类型
** @param block_idx 块在虚假函数中的索引
** @param seed 随机种子指针
** @return 成功返回0，失败返回-1
*/
static int emitFakeFunctionBlock (CFFContext *ctx, FakeFuncType func_type, 
                                   int block_idx, unsigned int *seed) {
  int reg_base = ctx->opaque_reg1;  /* 使用不透明谓词的寄存器 */
  
  CFF_LOG("  生成虚假函数块: 类型=%d, 块索引=%d", func_type, block_idx);
  
  switch (func_type) {
    case FAKE_FUNC_CALCULATOR: {
      /* 模拟一个计算函数：加载数值，进行运算 */
      for (int i = 0; i < FAKE_BLOCK_INSTS; i++) {
        NEXT_RAND(*seed);
        int val = (*seed % 200) - 100;
        Instruction inst;
        
        switch (i % 4) {
          case 0:
            inst = CREATE_ABx(OP_LOADI, reg_base, val + OFFSET_sBx);
            break;
          case 1:
            inst = CREATE_ABCk(OP_ADDI, reg_base + 1, reg_base, int2sC(val % 50), 0);
            break;
          case 2:
            inst = CREATE_ABCk(OP_MUL, reg_base, reg_base, reg_base + 1, 0);
            break;
          default:
            inst = CREATE_ABCk(OP_MMBIN, reg_base, reg_base + 1, 14, 0);
            break;
        }
        if (emitInstruction(ctx, inst) < 0) return -1;
      }
      break;
    }
    
    case FAKE_FUNC_STRING_OP: {
      /* 模拟字符串操作：移动寄存器，进行比较 */
      for (int i = 0; i < FAKE_BLOCK_INSTS; i++) {
        NEXT_RAND(*seed);
        Instruction inst;
        
        switch (i % 3) {
          case 0:
            inst = CREATE_ABCk(OP_MOVE, reg_base + (i % 2), reg_base, 0, 0);
            break;
          case 1:
            inst = CREATE_ABCk(OP_LEN, reg_base, reg_base + 1, 0, 0);
            break;
          default:
            inst = CREATE_ABx(OP_LOADI, reg_base, (*seed % 100) + OFFSET_sBx);
            break;
        }
        if (emitInstruction(ctx, inst) < 0) return -1;
      }
      break;
    }
    
    case FAKE_FUNC_TABLE_OP: {
      /* 模拟表操作：设置/获取字段 */
      for (int i = 0; i < FAKE_BLOCK_INSTS; i++) {
        NEXT_RAND(*seed);
        Instruction inst;
        
        switch (i % 3) {
          case 0:
            inst = CREATE_ABx(OP_LOADI, reg_base, (*seed % 50) + OFFSET_sBx);
            break;
          case 1:
            inst = CREATE_ABCk(OP_MOVE, reg_base + 1, reg_base, 0, 0);
            break;
          default:
            inst = CREATE_ABCk(OP_ADD, reg_base, reg_base, reg_base + 1, 0);
            break;
        }
        if (emitInstruction(ctx, inst) < 0) return -1;
      }
      break;
    }
    
    default: {
      /* 模拟循环：计数器操作 */
      for (int i = 0; i < FAKE_BLOCK_INSTS; i++) {
        NEXT_RAND(*seed);
        Instruction inst;
        
        switch (i % 4) {
          case 0:
            inst = CREATE_ABx(OP_LOADI, reg_base, block_idx + OFFSET_sBx);
            break;
          case 1:
            inst = CREATE_ABCk(OP_ADDI, reg_base, reg_base, int2sC(1), 0);
            break;
          case 2:
            inst = CREATE_ABCk(OP_MMBIN, reg_base, reg_base, 6, 0);
            break;
          default:
            inst = CREATE_ABCk(OP_MOVE, reg_base + 1, reg_base, 0, 0);
            break;
        }
        if (emitInstruction(ctx, inst) < 0) return -1;
      }
      break;
    }
  }
  
  return 0;
}


/*
** 生成虚假函数的完整代码路径
** @param ctx 上下文
** @param func_id 虚假函数ID
** @param seed 随机种子指针
** @param jmp_pcs 输出：每个块的跳转PC数组
** @return 成功返回生成的块数，失败返回-1
**
** 虚假函数结构：
**   ; 函数ID检查
**   EQI func_id_reg, func_id, k=1
**   JMP fake_func_block_0
**   
**   fake_func_block_0:
**     ; 虚假代码
**     LOADI state_reg, next_state
**     JMP dispatcher
**   
**   fake_func_block_1:
**     ; 虚假代码
**     ...
*/
static int emitFakeFunction (CFFContext *ctx, int func_id, unsigned int *seed,
                              int *entry_jmp_pc) {
  int func_id_reg = ctx->func_id_reg;
  int state_reg = ctx->state_reg;
  int num_blocks = FAKE_FUNC_BLOCKS;
  FakeFuncType func_type = (FakeFuncType)(func_id % 4);
  
  CFF_LOG("--- 生成虚假函数 %d (类型=%d) ---", func_id, func_type);
  
  /* 生成函数ID检查 */
  int encoded_func_id = func_id + 100;  /* 偏移以区分真实状态 */
  if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
    encoded_func_id = luaO_encodeState(encoded_func_id, ctx->seed ^ 0xABCDEF00);
  }
  
  CFF_LOG("  [PC=%d] EQI R[%d], %d, k=1 (虚假函数%d入口)", 
          ctx->new_code_size, func_id_reg, encoded_func_id, func_id);
  Instruction cmp = CREATE_ABCk(OP_EQI, func_id_reg, int2sC(encoded_func_id), 0, 1);
  if (emitInstruction(ctx, cmp) < 0) return -1;
  
  /* 跳转到虚假函数第一个块（偏移量待定） */
  Instruction jmp = CREATE_sJ(OP_JMP, 0, 0);
  *entry_jmp_pc = emitInstruction(ctx, jmp);
  if (*entry_jmp_pc < 0) return -1;
  
  return num_blocks;
}


/*
** 生成虚假函数的所有基本块
** @param ctx 上下文
** @param func_id 虚假函数ID
** @param seed 随机种子指针
** @param entry_jmp_pc 入口跳转PC（需要修正）
** @return 成功返回0，失败返回-1
*/
static int emitFakeFunctionBlocks (CFFContext *ctx, int func_id, unsigned int *seed,
                                    int entry_jmp_pc) {
  int state_reg = ctx->state_reg;
  int num_blocks = FAKE_FUNC_BLOCKS;
  FakeFuncType func_type = (FakeFuncType)(func_id % 4);
  
  /* 记录第一个块的位置并修正入口跳转 */
  int first_block_pc = ctx->new_code_size;
  int offset = first_block_pc - entry_jmp_pc - 1;
  SETARG_sJ(ctx->new_code[entry_jmp_pc], offset);
  
  CFF_LOG("  修正虚假函数%d入口跳转: PC=%d -> PC=%d", func_id, entry_jmp_pc, first_block_pc);
  
  /* 生成虚假函数的所有块 */
  for (int b = 0; b < num_blocks; b++) {
    CFF_LOG("  虚假函数%d 块%d @ PC=%d", func_id, b, ctx->new_code_size);
    
    /* 生成块内容 */
    if (emitFakeFunctionBlock(ctx, func_type, b, seed) < 0) return -1;
    
    /* 生成状态转换：跳转到下一个虚假块或回分发器 */
    NEXT_RAND(*seed);
    int next_state;
    if (b < num_blocks - 1) {
      /* 跳转到虚假函数的下一个块 */
      next_state = (func_id + 100) * 10 + b + 1;
    } else {
      /* 最后一个块：跳回分发器（可能跳到另一个虚假函数） */
      NEXT_RAND(*seed);
      next_state = (*seed % ctx->num_blocks);  /* 跳到一个真实块 */
    }
    
    if (ctx->obfuscate_flags & OBFUSCATE_STATE_ENCODE) {
      next_state = luaO_encodeState(next_state, ctx->seed);
    }
    
    Instruction state_inst = CREATE_ABx(OP_LOADI, state_reg, next_state + OFFSET_sBx);
    if (emitInstruction(ctx, state_inst) < 0) return -1;
    
    int jmp_offset = ctx->dispatcher_pc - ctx->new_code_size - 1;
    Instruction jmp_inst = CREATE_sJ(OP_JMP, jmp_offset + OFFSET_sJ, 0);
    if (emitInstruction(ctx, jmp_inst) < 0) return -1;
  }
  
  return 0;
}


/*
** =======================================================
** VM保护功能实现
** =======================================================
**
** VM保护将Lua字节码转换为自定义虚拟机指令集，
** 并使用加密保护，使静态分析更加困难。
**
** 实现步骤：
** 1. 生成随机操作码映射表
** 2. 将Lua指令转换为VM指令
** 3. 使用XOR加密指令
** 4. 生成VM解释器代码（嵌入到字节码中）
*/


/* VM保护相关常量 */
#define VM_CODE_INITIAL_CAPACITY  128   /* VM代码初始容量 */
#define VM_ENCRYPT_ROUNDS         3     /* 加密轮数 */


/*
** 初始化VM保护上下文
** @param L Lua状态
** @param f 函数原型
** @param seed 随机种子
** @return 上下文指针，失败返回NULL
**
** 功能描述：
** - 分配VM保护上下文结构
** - 生成随机操作码映射表
** - 初始化加密密钥
*/
VMProtectContext *luaO_initVMContext (lua_State *L, Proto *f, unsigned int seed) {
  VMProtectContext *ctx = (VMProtectContext *)luaM_malloc_(L, sizeof(VMProtectContext), 0);
  if (ctx == NULL) return NULL;
  
  ctx->L = L;
  ctx->f = f;
  ctx->vm_code = NULL;
  ctx->vm_code_size = 0;
  ctx->vm_code_capacity = 0;
  ctx->seed = seed;
  
  /* 生成加密密钥（基于种子） */
  unsigned int r = seed;
  NEXT_RAND(r);
  ctx->encrypt_key = ((uint64_t)r << 32);
  NEXT_RAND(r);
  ctx->encrypt_key |= r;
  
  /* 分配操作码映射表 */
  ctx->opcode_map = (int *)luaM_malloc_(L, sizeof(int) * NUM_OPCODES, 0);
  ctx->reverse_map = (int *)luaM_malloc_(L, sizeof(int) * VM_MAP_SIZE, 0);  /* 修复：使用NUM_OPCODES而非VM_OP_COUNT */
  
  if (ctx->opcode_map == NULL || ctx->reverse_map == NULL) {
    if (ctx->opcode_map) luaM_free_(L, ctx->opcode_map, sizeof(int) * NUM_OPCODES);
    if (ctx->reverse_map) luaM_free_(L, ctx->reverse_map, sizeof(int) * VM_MAP_SIZE);
    luaM_free_(L, ctx, sizeof(VMProtectContext));
    return NULL;
  }
  
  /* 初始化映射表 */
  for (int i = 0; i < NUM_OPCODES; i++) {
    ctx->opcode_map[i] = 0;  /* 默认映射到0 */
  }
  for (int i = 0; i < VM_MAP_SIZE; i++) {
    ctx->reverse_map[i] = 0;  /* 修复：初始化为0而非-1，避免序列化时64位负数溢出 */
  }
  
  /* 生成混淆的操作码映射 */
  r = seed ^ 0xDEADBEEF;
  
  int *used = (int *)luaM_malloc_(L, sizeof(int) * VM_MAP_SIZE, 0);
  memset(used, 0, sizeof(int) * VM_MAP_SIZE);
  for (int i = 0; i < NUM_OPCODES; i++) {
    int val;
    do {
      NEXT_RAND(r);
      val = r % VM_MAP_SIZE;
    } while (used[val]);
    used[val] = 1;
    ctx->opcode_map[i] = val;
    ctx->reverse_map[val] = i;
  }
  luaM_free_(L, used, sizeof(int) * VM_MAP_SIZE);
  
  CFF_LOG("VM上下文初始化完成: encrypt_key=0x%016llx", (unsigned long long)ctx->encrypt_key);
  
  return ctx;
}


/*
** 释放VM保护上下文
** @param ctx 要释放的上下文
*/
void luaO_freeVMContext (VMProtectContext *ctx) {
  if (ctx == NULL) return;
  
  lua_State *L = ctx->L;
  
  if (ctx->vm_code != NULL) {
    luaM_free_(L, ctx->vm_code, sizeof(VMInstruction) * ctx->vm_code_capacity);
  }
  
  if (ctx->opcode_map != NULL) {
    luaM_free_(L, ctx->opcode_map, sizeof(int) * NUM_OPCODES);
  }
  
  if (ctx->reverse_map != NULL) {
    luaM_free_(L, ctx->reverse_map, sizeof(int) * VM_MAP_SIZE);  /* 修复：使用NUM_OPCODES */
  }
  
  luaM_free_(L, ctx, sizeof(VMProtectContext));
}


/*
** 确保VM代码数组有足够容量
** @param ctx VM上下文
** @param needed 需要的额外空间
** @return 成功返回0，失败返回-1
*/
static int ensureVMCodeCapacity (VMProtectContext *ctx, int needed) {
  int required = ctx->vm_code_size + needed;
  
  if (required <= ctx->vm_code_capacity) {
    return 0;
  }
  
  int new_capacity = ctx->vm_code_capacity == 0 ? VM_CODE_INITIAL_CAPACITY : ctx->vm_code_capacity;
  while (new_capacity < required) {
    new_capacity *= 2;
  }
  
  VMInstruction *new_code;
  if (ctx->vm_code == NULL) {
    new_code = (VMInstruction *)luaM_malloc_(ctx->L, sizeof(VMInstruction) * new_capacity, 0);
  } else {
    new_code = (VMInstruction *)luaM_realloc_(
      ctx->L, ctx->vm_code,
      sizeof(VMInstruction) * ctx->vm_code_capacity,
      sizeof(VMInstruction) * new_capacity
    );
  }
  
  if (new_code == NULL) return -1;
  
  ctx->vm_code = new_code;
  ctx->vm_code_capacity = new_capacity;
  return 0;
}


/*
** 添加VM指令
** @param ctx VM上下文
** @param inst VM指令
** @return 指令的PC，失败返回-1
*/
static int emitVMInstruction (VMProtectContext *ctx, VMInstruction inst) {
  if (ensureVMCodeCapacity(ctx, 1) != 0) return -1;
  
  int pc = ctx->vm_code_size++;
  ctx->vm_code[pc] = inst;
  return pc;
}


/*
** 加密VM指令
** @param inst 原始指令
** @param key 加密密钥
** @param pc 指令位置（用于位置相关加密）
** @return 加密后的指令
**
** 加密算法：
** 1. XOR with key
** 2. 位旋转（基于PC）
** 3. 再次XOR
*/
static VMInstruction encryptVMInstruction (VMInstruction inst, uint64_t key, int pc) {
  uint64_t encrypted = inst;
  
  /* 第一轮XOR */
  encrypted ^= key;
  
  /* 位旋转（基于PC的量） */
  int rotate_amount = (pc & 63);
  encrypted = (encrypted << rotate_amount) | (encrypted >> (64 - rotate_amount));
  
  /* 第二轮XOR（使用修改过的密钥） */
  uint64_t modified_key = key ^ ((uint64_t)pc * 0x9E3779B97F4A7C15ULL);
  encrypted ^= modified_key;
  
  return encrypted;
}


/*
** 解密VM指令
** @param inst 加密的指令
** @param key 解密密钥
** @param pc 指令位置
** @return 解密后的指令
*/
static VMInstruction decryptVMInstruction (VMInstruction inst, uint64_t key, int pc) {
  uint64_t decrypted = inst;
  
  /* 逆向第二轮XOR */
  uint64_t modified_key = key ^ ((uint64_t)pc * 0x9E3779B97F4A7C15ULL);
  decrypted ^= modified_key;
  
  /* 逆向位旋转 */
  int rotate_amount = (pc % 64);
  decrypted = (decrypted >> rotate_amount) | (decrypted << (64 - rotate_amount));
  
  /* 逆向第一轮XOR */
  decrypted ^= key;
  
  return decrypted;
}


/*
** Try to convert a 'for' limit to an integer, preserving the semantics
** of the loop.
*/
static int forlimit (lua_State *L, lua_Integer init, const TValue *lim,
                                   lua_Integer *p, lua_Integer step) {
  if (!luaV_tointeger(lim, p, (step < 0 ? F2Iceil : F2Ifloor))) {
    /* not coercible to in integer */
    lua_Number flim;  /* try to convert to float */
    if (!tonumber(lim, &flim)) /* cannot convert to float? */
      luaG_forerror(L, lim, "limit");
    /* else 'flim' is a float out of integer bounds */
    if (luai_numlt(0, flim)) {  /* if it is positive, it is too large */
      if (step < 0) return 1;  /* initial value must be less than it */
      *p = LUA_MAXINTEGER;  /* truncate */
    }
    else {  /* it is less than min integer */
      if (step > 0) return 1;  /* initial value must be greater than it */
      *p = LUA_MININTEGER;  /* truncate */
    }
  }
  return (step > 0 ? init > *p : init < *p);  /* not to run? */
}


/*
** Prepare a numerical for loop (opcode OP_FORPREP).
*/
static int forprep (lua_State *L, StkId ra) {
  TValue *pinit = s2v(ra);
  TValue *plimit = s2v(ra + 1);
  TValue *pstep = s2v(ra + 2);
  if (ttisinteger(pinit) && ttisinteger(pstep)) { /* integer loop? */
    lua_Integer init = ivalue(pinit);
    lua_Integer step = ivalue(pstep);
    lua_Integer limit;
    if (step == 0)
      luaG_runerror(L, "'for' step is zero");
    setivalue(s2v(ra + 3), init);  /* control variable */
    if (forlimit(L, init, plimit, &limit, step))
      return 1;  /* skip the loop */
    else {  /* prepare loop counter */
      lua_Unsigned count;
      if (step > 0) {  /* ascending loop? */
        count = l_castS2U(limit) - l_castS2U(init);
        if (step != 1)  /* avoid division in the too common case */
          count /= l_castS2U(step);
      }
      else {  /* step < 0; descending loop */
        count = l_castS2U(init) - l_castS2U(limit);
        /* 'step+1' avoids negating 'mininteger' */
        count /= l_castS2U(-(step + 1)) + 1u;
      }
      /* store the counter in place of the limit (which won't be
         needed anymore) */
      setivalue(plimit, l_castU2S(count));
    }
  }
  else {  /* try making all values floats */
    lua_Number init; lua_Number limit; lua_Number step;
    if (l_unlikely(!tonumber(plimit, &limit)))
      luaG_forerror(L, plimit, "limit");
    if (l_unlikely(!tonumber(pstep, &step)))
      luaG_forerror(L, pstep, "step");
    if (l_unlikely(!tonumber(pinit, &init)))
      luaG_forerror(L, pinit, "initial value");
    if (step == 0)
      luaG_runerror(L, "'for' step is zero");
    if (luai_numlt(0, step) ? luai_numlt(limit, init)
                            : luai_numlt(init, limit))
      return 1;  /* skip the loop */
    else {
      /* make sure internal values are all floats */
      setfltvalue(plimit, limit);
      setfltvalue(pstep, step);
      setfltvalue(s2v(ra), init);  /* internal index */
      setfltvalue(s2v(ra + 3), init);  /* control variable */
    }
  }
  return 0;
}


/*
** Execute a step of a float numerical for loop
*/
static int floatforloop (lua_State *L, StkId ra) {
  lua_Number step = fltvalue(s2v(ra + 2));
  lua_Number limit = fltvalue(s2v(ra + 1));
  lua_Number idx = fltvalue(s2v(ra));  /* internal index */
  idx = luai_numadd(L, idx, step);  /* increment index */
  if (luai_numlt(0, step) ? luai_numle(idx, limit)
                          : luai_numle(limit, idx)) {
    chgfltvalue(s2v(ra), idx);  /* update internal index */
    setfltvalue(s2v(ra + 3), idx);  /* and control variable */
    return 1;  /* jump back */
  }
  else
    return 0;  /* finish the loop */
}


/*
** 将单条Lua指令转换为VM指令
*/
static int convertLuaInstToVM (VMProtectContext *ctx, Instruction inst, int pc) {
  OpCode lua_op = GET_OPCODE(inst);
  int vm_op = ctx->opcode_map[lua_op];
  int a = GETARG_A(inst);
  uint64_t vm_inst;
  enum OpMode mode = getOpMode(lua_op);
  switch (mode) {
    case iABx:
    case iAsBx:
      vm_inst = VM_MAKE_INST_BX(vm_op, a, (uint64_t)GETARG_Bx(inst));
      break;
    case iAx:
      vm_inst = VM_MAKE_INST_BX(vm_op, 0, (uint64_t)GETARG_Ax(inst));
      break;
    case isJ:
      vm_inst = VM_MAKE_INST_BX(vm_op, 0, (uint64_t)(GETARG_sJ(inst) + OFFSET_sJ));
      break;
    case ivABC:
      vm_inst = VM_MAKE_INST(vm_op, a, GETARG_vB(inst), GETARG_vC(inst), GETARG_k(inst));
      break;
    default:
      vm_inst = VM_MAKE_INST(vm_op, a, GETARG_B(inst), GETARG_C(inst), GETARG_k(inst));
      break;
  }
  VMInstruction encrypted = encryptVMInstruction(vm_inst, ctx->encrypt_key, pc);
  CFF_LOG("  [PC=%d] Lua %s -> VM op=%d, encrypted=0x%016llx", 
          pc, getOpName(lua_op), vm_op, (unsigned long long)encrypted);
  if (emitVMInstruction(ctx, encrypted) < 0) return -1;
  return 0;
}

/*
** 将Lua字节码转换为VM指令
** @param ctx VM保护上下文
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** - 遍历函数的所有Lua指令
** - 将每条指令转换为对应的VM指令
** - 应用加密保护
*/
int luaO_convertToVM (VMProtectContext *ctx) {
  Proto *f = ctx->f;
  
  CFF_LOG("========== 开始转换Lua字节码到VM指令 ==========");
  CFF_LOG("原始代码大小: %d 条指令", f->sizecode);
  
  /* 遍历所有Lua指令并转换 */
  for (int pc = 0; pc < f->sizecode; pc++) {
    Instruction inst = f->code[pc];
    
    if (convertLuaInstToVM(ctx, inst, pc) != 0) {
      CFF_LOG("转换失败 @ PC=%d", pc);
      return -1;
    }
  }
  
  /* 添加结束标记 */
  VMInstruction halt = VM_MAKE_INST(VM_OP_HALT, 0, 0, 0, 0);
  VMInstruction encrypted_halt = encryptVMInstruction(halt, ctx->encrypt_key, f->sizecode);
  if (emitVMInstruction(ctx, encrypted_halt) < 0) return -1;
  
  CFF_LOG("========== VM转换完成，共 %d 条VM指令 ==========", ctx->vm_code_size);
  
  return 0;
}


/*
** 生成VM解释器的Lua字节码
** @param ctx VM上下文
** @param out_code 输出代码数组
** @param out_size 输出代码大小
** @return 成功返回0，失败返回-1
**
** 功能描述：
** 生成一个内联的VM解释器，使用Lua字节码实现。
** 解释器循环：
** 1. 读取加密的VM指令
** 2. 解密指令
** 3. 根据VM操作码执行相应操作
** 4. 更新PC
**
** 注意：完整的VM解释器非常复杂，这里实现一个简化版本。
** 简化版本会在运行时还原部分指令。
*/
static int generateVMInterpreter (VMProtectContext *ctx, 
                                   Instruction **out_code, int *out_size) {
  lua_State *L = ctx->L;
  Proto *f = ctx->f;
  
  /* 
  ** 简化实现：
  ** 当前版本直接复制原始代码，不添加序言。
  ** VM保护的实际效果通过在difierline_mode中设置标志来实现，
  ** 运行时检测到该标志后可以启用额外的保护机制。
  ** 
  ** 完整的VM保护需要：
  ** 1. 将代码转换为自定义VM指令（已在luaO_convertToVM中完成）
  ** 2. 在运行时用自定义解释器执行（需要修改lvm.c）
  ** 
  ** 由于添加序言会破坏CFF生成的跳转偏移量，
  ** 当前采用不添加序言的方式保持兼容性。
  */
  
  /* 直接复制原始代码 */
  int total_size = f->sizecode;
  
  Instruction *new_code = (Instruction *)luaM_malloc_(L, sizeof(Instruction) * total_size, 0);
  if (new_code == NULL) return -1;
  
  /* 复制原始代码（经过混淆但保持可执行） */
  memcpy(new_code, f->code, sizeof(Instruction) * f->sizecode);
  
  *out_code = new_code;
  *out_size = total_size;
  
  CFF_LOG("生成VM解释器: 代码大小=%d", total_size);
  
  return 0;
}


/*
** =======================================================
** VM代码表管理函数
** =======================================================
*/


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
VMCodeTable *luaO_registerVMCode (lua_State *L, Proto *p,
                                   VMInstruction *code, int size,
                                   uint64_t key, int *reverse_map,
                                   unsigned int seed) {
  global_State *g = G(L);
  
  /* 分配 VMCodeTable 结构 */
  VMCodeTable *vt = (VMCodeTable *)luaM_malloc_(L, sizeof(VMCodeTable), 0);
  if (vt == NULL) return NULL;
  
  /* 复制 VM 指令数组 */
  vt->code = (VMInstruction *)luaM_malloc_(L, sizeof(VMInstruction) * size, 0);
  if (vt->code == NULL) {
    luaM_free_(L, vt, sizeof(VMCodeTable));
    return NULL;
  }
  memcpy(vt->code, code, sizeof(VMInstruction) * size);
  
  /* 复制反向映射表 */
  vt->reverse_map = (int *)luaM_malloc_(L, sizeof(int) * VM_MAP_SIZE, 0);
  if (vt->reverse_map == NULL) {
    luaM_free_(L, vt->code, sizeof(VMInstruction) * size);
    luaM_free_(L, vt, sizeof(VMCodeTable));
    return NULL;
  }
  memcpy(vt->reverse_map, reverse_map, sizeof(int) * VM_MAP_SIZE);
  
  /* 设置其他字段 */
  vt->proto = p;
  vt->size = size;
  vt->capacity = size;
  vt->encrypt_key = key;
  vt->seed = seed;
  
  /* 插入链表头部 */
  vt->next = g->vm_code_list;
  g->vm_code_list = vt;
  
  /* 设置 Proto 的 vm_code_table 指针 */
  p->vm_code_table = vt;
  
  CFF_LOG("注册VM代码: proto=%p, size=%d, key=0x%016llx", 
          (void*)p, size, (unsigned long long)key);
  
  return vt;
}


/*
** 查找函数关联的VM代码表
** @param L Lua状态
** @param p 函数原型
** @return 找到返回VMCodeTable指针，未找到返回NULL
*/
VMCodeTable *luaO_findVMCode (lua_State *L, Proto *p) {
  /* 优先使用 Proto 中的直接指针 */
  if (p->vm_code_table != NULL) {
    return p->vm_code_table;
  }
  
  /* 备用：遍历全局链表查找 */
  global_State *g = G(L);
  VMCodeTable *vt = g->vm_code_list;
  
  while (vt != NULL) {
    if (vt->proto == p) {
      p->vm_code_table = vt;  /* 缓存到 Proto */
      return vt;
    }
    vt = vt->next;
  }
  
  return NULL;
}


/*
** 释放所有VM代码表
** @param L Lua状态
*/
void luaO_freeAllVMCode (lua_State *L) {
  global_State *g = G(L);
  VMCodeTable *vt = g->vm_code_list;
  
  while (vt != NULL) {
    VMCodeTable *next = vt->next;
    
    /* 释放 VM 指令数组 */
    if (vt->code != NULL) {
      luaM_free_(L, vt->code, sizeof(VMInstruction) * vt->capacity);
    }
    
    /* 释放反向映射表 */
    if (vt->reverse_map != NULL) {
      luaM_free_(L, vt->reverse_map, sizeof(int) * VM_MAP_SIZE);
    }
    
    /* 清除 Proto 中的指针 */
    if (vt->proto != NULL) {
      vt->proto->vm_code_table = NULL;
    }
    
    /* 释放 VMCodeTable 结构 */
    luaM_free_(L, vt, sizeof(VMCodeTable));
    
    vt = next;
  }
  
  g->vm_code_list = NULL;
}


/*
** 解密单条VM指令
** @param encrypted 加密的指令
** @param key 加密密钥
** @param pc 程序计数器位置
** @return 解密后的指令
*/
static VMInstruction decryptVMInst (VMInstruction encrypted, uint64_t key, int pc) {
  uint64_t decrypted = encrypted;
  
  /* 逆向加密过程 */
  /* 第二轮XOR (逆向) */
  uint64_t modified_key = key ^ ((uint64_t)pc * 0x9E3779B97F4A7C15ULL);
  decrypted ^= modified_key;
  
  /* 位旋转 (逆向 - 右旋转) */
  int rotate_amount = (pc & 63);
  decrypted = (decrypted >> rotate_amount) | (decrypted << (64 - rotate_amount));
  
  /* 第一轮XOR (逆向) */
  decrypted ^= key;
  
  return decrypted;
}


/*
** 执行VM保护的代码
** @param L Lua状态
** @param f 函数原型（包含VM代码）
** @return 执行结果: 0成功, -1失败, 1表示需要回退到原生VM
**
** 功能描述：
** 这是VM解释器的核心函数。
** 它读取加密的VM指令，解密并执行。
**
** 执行流程：
** 1. 获取VMCodeTable
** 2. 解密当前指令
** 3. 映射VM操作码到Lua操作码
** 4. 根据操作码执行相应操作
** 5. 更新PC继续执行
*/

#define savepc(L)	(ci->u.l.savedpc = (const Instruction *)(f->code + pc))
#define checkGC(L,c)  \
	{ luaC_condGC(L, (savepc(L), L->top.p = (c)), \
                         ((void)0)); \
           luai_threadyield(L); }

int luaO_executeVM (lua_State *L, Proto *f) {
  if (!(f->difierline_mode & OBFUSCATE_VM_PROTECT)) return 1;
  VMCodeTable *vm = luaO_findVMCode(L, f);
  if (vm == NULL) return 1;
  CallInfo *ci = L->ci;
  LClosure *cl = clLvalue(s2v(ci->func.p));
  TValue *k = f->k;
  StkId base = ci->func.p + 1;
  int pc = (int)(ci->u.l.savedpc - f->code);
  lua_Number nb, nc;
  while (pc < vm->size) {
    VMInstruction decrypted = decryptVMInst(vm->code[pc], vm->encrypt_key, pc);
    int vm_op = VM_GET_OP(decrypted), a = VM_GET_A(decrypted), b = VM_GET_B(decrypted), c = VM_GET_C(decrypted), flags = VM_GET_FLAGS(decrypted);
    int64_t bx = VM_GET_Bx(decrypted);
    int lua_op = vm->reverse_map[vm_op];
    
    if (lua_op < 0 || lua_op >= NUM_OPCODES) {
       if (vm_op == VM_OP_HALT) return 0;
       ci->u.l.savedpc = (const Instruction *)(f->code + pc);
       return 1;
    }
    
    switch (lua_op) {
      case OP_MOVE: { setobjs2s(L, base + a, base + b); break; }
      case OP_LOADI: { setivalue(s2v(base + a), (lua_Integer)(bx - OFFSET_sBx)); break; }
      case OP_LOADK: { if (bx >= 0 && bx < f->sizek) setobj2s(L, base + a, k + bx); break; }
      case OP_LOADF: { setfltvalue(s2v(base + a), cast_num((lua_Integer)(bx - OFFSET_sBx))); break; }
      case OP_LOADKX: { pc++; if (pc < f->sizecode) setobj2s(L, base + a, k + GETARG_Ax(f->code[pc])); break; }
      case OP_LOADFALSE: { setbfvalue(s2v(base + a)); break; }
      case OP_LOADTRUE: { setbtvalue(s2v(base + a)); break; }
      case OP_LOADNIL: { StkId ra = base + a; for (int i=0; i<=b; i++) setnilvalue(s2v(ra++)); break; }
      case OP_GETTABUP: { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; }
      case OP_SETUPVAL: { if (b < cl->nupvalues) { UpVal *uv = cl->upvals[b]; setobj(L, uv->v.p, s2v(base + a)); luaC_barrier(L, uv, s2v(base + a)); } break; }
      case OP_GETTABLE: { const TValue *slot; if (luaV_fastget(L, s2v(base + b), s2v(base + c), slot, luaH_get)) { setobj2s(L, base + a, slot); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishget(L, s2v(base + b), s2v(base + c), base + a, slot); break; } break; }
      case OP_SETTABLE: { const TValue *slot; TValue *rc = (flags) ? k + c : s2v(base + c); if (luaV_fastget(L, s2v(base + a), s2v(base + b), slot, luaH_get)) { luaV_finishfastset(L, s2v(base + a), slot, rc); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishset(L, s2v(base + a), s2v(base + b), rc, slot); break; } break; }
      case OP_GETI: { const TValue *slot; if (luaV_fastgeti(L, s2v(base + b), c, slot)) { setobj2s(L, base + a, slot); } else { TValue key; setivalue(&key, c); ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishget(L, s2v(base + b), &key, base + a, slot); break; } break; }
      case OP_SETI: { const TValue *slot; TValue *rc = (flags) ? k + c : s2v(base + c); if (luaV_fastgeti(L, s2v(base + a), b, slot)) { luaV_finishfastset(L, s2v(base + a), slot, rc); } else { TValue key; setivalue(&key, b); ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishset(L, s2v(base + a), &key, rc, slot); break; } break; }
      case OP_GETFIELD: { const TValue *slot; TValue *rc = k + c; if (luaV_fastget(L, s2v(base + b), tsvalue(rc), slot, luaH_getshortstr)) { setobj2s(L, base + a, slot); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishget(L, s2v(base + b), rc, base + a, slot); break; } break; }
      case OP_SETFIELD: { const TValue *slot; TValue *rb = k + b, *rc = (flags) ? k + c : s2v(base + c); if (luaV_fastget(L, s2v(base + a), tsvalue(rb), slot, luaH_getshortstr)) { luaV_finishfastset(L, s2v(base + a), slot, rc); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishset(L, s2v(base + a), rb, rc, slot); break; } break; }
      case OP_NEWTABLE: { ci->u.l.savedpc = (const Instruction *)(f->code + pc); int asize = c; if (flags) { pc++; if (pc < f->sizecode) asize += GETARG_Ax(f->code[pc]) * (MAXARG_C + 1); } L->top.p = base + a + 1; Table *t_ = luaH_new(L); sethvalue2s(L, base + a, t_); if (b || asize) { int hsize = (b > 0) ? (1u << (b - 1)) : 0; luaH_resize(L, t_, asize, hsize); } break; }
      case OP_SELF: { TValue *rb = s2v(base + b), *rc = (flags) ? k + c : s2v(base + c); setobj2s(L, base + a + 1, rb); const TValue *slot; if (luaV_fastget(L, rb, tsvalue(rc), slot, luaH_getstr)) { setobj2s(L, base + a, slot); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_finishget(L, rb, rc, base + a, slot); break; } break; }
      case OP_ADDI: { TValue *rb = s2v(base + b); int imm = sC2int(c); if (ttispointer(rb)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rb) + imm); } else if (ttisinteger(rb)) { setivalue(s2v(base + a), intop(+, ivalue(rb), (lua_Integer)imm)); } else if (tonumberns(rb, nb)) { setfltvalue(s2v(base + a), luai_numadd(L, nb, cast_num(imm))); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_ADDK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (ttispointer(rb) && ttisinteger(rc)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rb) + ivalue(rc)); } else if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(+, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numadd(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SUBK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (ttispointer(rb) && ttisinteger(rc)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rb) - ivalue(rc)); } else if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(-, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numsub(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_MULK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(*, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_nummul(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_MODK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), luaV_mod(L, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luaV_modf(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_POWK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numpow(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_DIVK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numdiv(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_IDIVK: { TValue *rb = s2v(base + b); TValue *rc = k + c; if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), luaV_idiv(L, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numidiv(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_ADD: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (ttispointer(rb) && ttisinteger(rc)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rb) + ivalue(rc)); } else if (ttisinteger(rb) && ttispointer(rc)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rc) + ivalue(rb)); } else if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(+, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numadd(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SUB: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (ttispointer(rb) && ttisinteger(rc)) { setptrvalue(s2v(base + a), (char *)ptrvalue(rb) - ivalue(rc)); } else if (ttispointer(rb) && ttispointer(rc)) { setivalue(s2v(base + a), (char *)ptrvalue(rb) - (char *)ptrvalue(rc)); } else if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(-, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numsub(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_MUL: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), intop(*, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_nummul(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_MOD: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), luaV_mod(L, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luaV_modf(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_POW: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numpow(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_DIV: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numdiv(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_IDIV: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); if (ttisinteger(rb) && ttisinteger(rc)) { setivalue(s2v(base + a), luaV_idiv(L, ivalue(rb), ivalue(rc))); } else if (tonumberns(rb, nb) && tonumberns(rc, nc)) { setfltvalue(s2v(base + a), luai_numidiv(L, nb, nc)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BANDK: { TValue *rb = s2v(base + b); TValue *rc = k + c; lua_Integer i1; if (tointegerns(rb, &i1)) { setivalue(s2v(base + a), intop(&, i1, ivalue(rc))); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BORK: { TValue *rb = s2v(base + b); TValue *rc = k + c; lua_Integer i1; if (tointegerns(rb, &i1)) { setivalue(s2v(base + a), intop(|, i1, ivalue(rc))); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BXORK: { TValue *rb = s2v(base + b); TValue *rc = k + c; lua_Integer i1; if (tointegerns(rb, &i1)) { setivalue(s2v(base + a), intop(^, i1, ivalue(rc))); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SHLI: { TValue *rb = s2v(base + b); int ic = sC2int(c); lua_Integer ib; if (tointegerns(rb, &ib)) { setivalue(s2v(base + a), luaV_shiftl(ib, ic)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SHRI: { TValue *rb = s2v(base + b); int ic = sC2int(c); lua_Integer ib; if (tointegerns(rb, &ib)) { setivalue(s2v(base + a), luaV_shiftl(ib, -ic)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BAND: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer i1, i2; if (tointegerns(rb, &i1) && tointegerns(rc, &i2)) { setivalue(s2v(base + a), intop(&, i1, i2)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BOR: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer i1, i2; if (tointegerns(rb, &i1) && tointegerns(rc, &i2)) { setivalue(s2v(base + a), intop(|, i1, i2)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BXOR: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer i1, i2; if (tointegerns(rb, &i1) && tointegerns(rc, &i2)) { setivalue(s2v(base + a), intop(^, i1, i2)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SHL: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer i1, i2; if (tointegerns(rb, &i1) && tointegerns(rc, &i2)) { setivalue(s2v(base + a), luaV_shiftl(i1, i2)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_SHR: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer i1, i2; if (tointegerns(rb, &i1) && tointegerns(rc, &i2)) { setivalue(s2v(base + a), luaV_shiftl(i1, -i2)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_NOT: { if (l_isfalse(s2v(base + b))) { setbtvalue(s2v(base + a)); } else { setbfvalue(s2v(base + a)); } break; }
      case OP_UNM: { TValue *rb = s2v(base + b); lua_Number nb; if (ttisinteger(rb)) { setivalue(s2v(base + a), intop(-, 0, ivalue(rb))); } else if (tonumberns(rb, nb)) { setfltvalue(s2v(base + a), luai_numunm(L, nb)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_BNOT: { TValue *rb = s2v(base + b); lua_Integer ib; if (tointegerns(rb, &ib)) { setivalue(s2v(base + a), intop(^, ~l_castS2U(0), ib)); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_LEN: { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = ci->top.p; luaV_objlen(L, base + a, s2v(base + b)); break; }
      case OP_CONCAT: { ci->u.l.savedpc = (const Instruction *)(f->code + pc); L->top.p = base + a + b; luaV_concat(L, b); break; }
      case OP_JMP: { pc += (int)(bx - OFFSET_sJ) + 1; continue; }
      case OP_EQ: { if (luaV_equalobj(L, s2v(base + a), s2v(base + b)) != flags) pc++; break; }
      case OP_LT: { if (luaV_lessthan(L, s2v(base + a), s2v(base + b)) != flags) pc++; break; }
      case OP_LE: { if (luaV_lessequal(L, s2v(base + a), s2v(base + b)) != flags) pc++; break; }
      case OP_EQK: { if (luaV_equalobj(L, s2v(base + a), k + b) != flags) pc++; break; }
      case OP_EQI: { TValue *ra_v = s2v(base + a); int cond = ttisinteger(ra_v) ? (ivalue(ra_v) == (lua_Integer)sC2int(b)) : (ttisfloat(ra_v) ? luai_numeq(fltvalue(ra_v), cast_num(sC2int(b))) : 0); if (cond != flags) pc++; break; }
      case OP_LTI: { TValue *ra_v = s2v(base + a); int cond = ttisinteger(ra_v) ? (ivalue(ra_v) < (lua_Integer)sC2int(b)) : (ttisfloat(ra_v) ? luai_numlt(fltvalue(ra_v), cast_num(sC2int(b))) : 0); if (cond != flags) pc++; break; }
      case OP_LEI: { TValue *ra_v = s2v(base + a); int cond = ttisinteger(ra_v) ? (ivalue(ra_v) <= (lua_Integer)sC2int(b)) : (ttisfloat(ra_v) ? luai_numle(fltvalue(ra_v), cast_num(sC2int(b))) : 0); if (cond != flags) pc++; break; }
      case OP_GTI: { TValue *ra_v = s2v(base + a); int cond = ttisinteger(ra_v) ? (ivalue(ra_v) > (lua_Integer)sC2int(b)) : (ttisfloat(ra_v) ? luai_numgt(fltvalue(ra_v), cast_num(sC2int(b))) : 0); if (cond != flags) pc++; break; }
      case OP_GEI: { TValue *ra_v = s2v(base + a); int cond = ttisinteger(ra_v) ? (ivalue(ra_v) >= (lua_Integer)sC2int(b)) : (ttisfloat(ra_v) ? luai_numge(fltvalue(ra_v), cast_num(sC2int(b))) : 0); if (cond != flags) pc++; break; }
      case OP_TEST: { if (l_isfalse(s2v(base + a)) == flags) pc++; break; }
      case OP_TESTSET: { TValue *rb = s2v(base + b); if (l_isfalse(rb) == flags) pc++; else setobj2s(L, base + a, rb); break; }
      case OP_SPACESHIP: { TValue *rb = s2v(base + b); TValue *rc = s2v(base + c); lua_Integer res; if (ttisinteger(rb) && ttisinteger(rc)) { lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc); res = (ib < ic) ? -1 : ((ib > ic) ? 1 : 0); setivalue(s2v(base + a), res); } else if (ttisnumber(rb) && ttisnumber(rc)) { lua_Number nb, nc; luaV_tonumber_(rb, &nb); luaV_tonumber_(rc, &nc); res = (nb < nc) ? -1 : ((nb > nc) ? 1 : 0); setivalue(s2v(base + a), res); } else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } break; }
      case OP_VARARG: { int n = c - 1; int vatab = flags ? b : -1; ci->u.l.savedpc = (const Instruction *)(f->code + pc); luaT_getvarargs(L, ci, base + a, n); if (vatab >= 0) { /* handle vatab */ } break; }
      case OP_GETVARG: { luaT_getvararg(L, ci, base + a, s2v(base + c)); break; }
      case OP_VARARGPREP: { luaT_adjustvarargs(L, a, ci, cl->p); base = ci->func.p + 1; break; }
      case OP_IS: { TValue *ra = s2v(base + a); TValue *rb = k + b; const char *typename_expected = getstr(tsvalue(rb)); const char *typename_actual; const TValue *tm = luaT_gettmbyobj(L, ra, TM_TYPE); if (!notm(tm) && ttisstring(tm)) typename_actual = getstr(tsvalue(tm)); else typename_actual = luaT_objtypename(L, ra); if ((strcmp(typename_actual, typename_expected) == 0) != flags) pc++; break; }
      case OP_TESTNIL: { TValue *rb = s2v(base + b); if (ttisnil(rb) != flags) pc++; break; }
      case OP_IN: { StkId ra = base + a; TValue *va = s2v(base + b); TValue *vb = s2v(base + c); if (ttisstring(va) && ttisstring(vb)) { const char *s1 = getstr(tsvalue(va)); const char *s2 = getstr(tsvalue(vb)); size_t l1 = tsslen(tsvalue(va)); size_t l2 = tsslen(tsvalue(vb)); int found = 0; if (l1 <= l2) { size_t i; for (i = 0; i <= l2 - l1; i++) { if (memcmp(s2 + i, s1, l1) == 0) { found = 1; break; } } } if (found) setbtvalue(s2v(ra)); else setbfvalue(s2v(ra)); } else { if (l_unlikely(!ttistable(vb))) { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } const TValue *res = luaH_get(hvalue(vb), va); if (!ttisnil(res)) setbtvalue(s2v(ra)); else setbfvalue(s2v(ra)); } break; }
      case OP_SLICE: { StkId ra = base + a; StkId base_reg = base + b; TValue *src_table = s2v(base_reg); TValue *start_val = s2v(base_reg + 1); TValue *end_val = s2v(base_reg + 2); TValue *step_val = s2v(base_reg + 3); Table *t; Table *result_t; lua_Integer tlen; lua_Integer start_idx, end_idx, step; lua_Integer result_idx; if (l_unlikely(!ttistable(src_table))) { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } t = hvalue(src_table); tlen = luaH_getn(t); if (ttisnil(start_val)) start_idx = 1; else if (ttisinteger(start_val)) start_idx = ivalue(start_val); else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } if (ttisnil(end_val)) end_idx = tlen; else if (ttisinteger(end_val)) end_idx = ivalue(end_val); else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } if (ttisnil(step_val)) step = 1; else if (ttisinteger(step_val)) step = ivalue(step_val); else { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } if (step == 0) { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; } if (start_idx < 0) start_idx = tlen + start_idx + 1; if (end_idx < 0) end_idx = tlen + end_idx + 1; if (step > 0) { if (start_idx < 1) start_idx = 1; if (end_idx > tlen) end_idx = tlen; } else { if (start_idx > tlen) start_idx = tlen; if (end_idx < 1) end_idx = 1; } L->top.p = ra + 1; result_t = luaH_new(L); sethvalue2s(L, ra, result_t); result_idx = 1; if (step > 0) { lua_Integer idx; for (idx = start_idx; idx <= end_idx; idx += step) { const TValue *val = luaH_getint(t, idx); if (!ttisnil(val)) { TValue temp; setobj(L, &temp, val); luaH_setint(L, result_t, result_idx, &temp); } result_idx++; } } else { lua_Integer idx; for (idx = start_idx; idx >= end_idx; idx += step) { const TValue *val = luaH_getint(t, idx); if (!ttisnil(val)) { TValue temp; setobj(L, &temp, val); luaH_setint(L, result_t, result_idx, &temp); } result_idx++; } } checkGC(L, ra + 1); break; }
      case OP_NEWCLASS: { TString *classname = tsvalue(&k[bx]); ci->u.l.savedpc = (const Instruction *)(f->code + pc); luaC_newclass(L, classname); setobj2s(L, base + a, s2v(L->top.p - 1)); L->top.p--; checkGC(L, base + a + 1); break; }
      case OP_INHERIT: { TValue *rb = s2v(base + b); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rb); L->top.p++; luaC_inherit(L, -2, -1); L->top.p -= 2; break; }
      case OP_GETSUPER: { TString *key = tsvalue(&k[c]); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + b)); L->top.p++; luaC_super(L, -1, key); setobj2s(L, base + a, s2v(L->top.p - 1)); L->top.p -= 2; break; }
      case OP_SETMETHOD: { TString *key = tsvalue(&k[b]); TValue *rc = s2v(base + c); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rc); L->top.p++; luaC_setmethod(L, -2, key, -1); L->top.p -= 2; break; }
      case OP_SETSTATIC: { TString *key = tsvalue(&k[b]); TValue *rc = s2v(base + c); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rc); L->top.p++; luaC_setstatic(L, -2, key, -1); L->top.p -= 2; break; }
      case OP_NEWOBJ: { TValue *rb = s2v(base + b); int nargs = c - 1; ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, rb); L->top.p++; for (int j = 0; j < nargs; j++) { setobj2s(L, L->top.p, s2v(base + a + 1 + j)); L->top.p++; } luaC_newobject(L, -(nargs + 1), nargs); setobj2s(L, base + a, s2v(L->top.p - 1)); L->top.p -= (nargs + 2); checkGC(L, base + a + 1); break; }
      case OP_GETPROP: { TString *key = tsvalue(&k[c]); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + b)); L->top.p++; luaC_getprop(L, -1, key); setobj2s(L, base + a, s2v(L->top.p - 1)); L->top.p -= 2; break; }
      case OP_SETPROP: { TString *key = tsvalue(&k[b]); TValue *rc = (flags) ? k + c : s2v(base + c); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rc); L->top.p++; luaC_setprop(L, -2, key, -1); L->top.p -= 2; break; }
      case OP_INSTANCEOF: { TValue *rb = s2v(base + b); luaD_checkstack(L, 2); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rb); L->top.p++; int result = luaC_instanceof(L, -2, -1); L->top.p -= 2; if (result != flags) pc++; break; }
      case OP_IMPLEMENT: { TValue *rb = s2v(base + b); ci->u.l.savedpc = (const Instruction *)(f->code + pc); setobj2s(L, L->top.p, s2v(base + a)); L->top.p++; setobj2s(L, L->top.p, rb); L->top.p++; luaC_implement(L, -2, -1); L->top.p -= 2; break; }
      case OP_SETIFACEFLAG: { if (ttistable(s2v(base + a))) { Table *t = hvalue(s2v(base + a)); TValue key, val; setsvalue(L, &key, luaS_newliteral(L, "__flags")); const TValue *oldflags = luaH_getstr(t, tsvalue(&key)); lua_Integer fl = ttisinteger(oldflags) ? ivalue(oldflags) : 0; fl |= CLASS_FLAG_INTERFACE; setivalue(&val, fl); luaH_set(L, t, &key, &val); } break; }
      case OP_ADDMETHOD: { TString *method_name = tsvalue(&k[b]); int param_count = c; if (ttistable(s2v(base + a))) { Table *t = hvalue(s2v(base + a)); TValue key; setsvalue(L, &key, luaS_newliteral(L, "__methods")); const TValue *methods_tv = luaH_getstr(t, tsvalue(&key)); if (ttistable(methods_tv)) { Table *methods = hvalue(methods_tv); TValue method_key, method_val; setsvalue(L, &method_key, method_name); setivalue(&method_val, param_count); luaH_set(L, methods, &method_key, &method_val); } } break; }
      case OP_CASE: { StkId ra = base + a; TValue rb; setobj(L, &rb, s2v(base + b)); TValue rc; setobj(L, &rc, s2v(base + c)); Table *t; L->top.p = ra + 1; t = luaH_new(L); sethvalue2s(L, ra, t); luaH_setint(L, t, 1, &rb); luaH_setint(L, t, 2, &rc); checkGC(L, ra + 1); break; }
      case OP_CALL: { StkId ra = base + a; if (b) L->top.p = ra + b; ci->u.l.savedpc = (const Instruction *)(f->code + pc + 1); if (luaD_precall(L, ra, c - 1)) { luaV_execute(L, L->ci); } base = ci->func.p + 1; break; }
      case OP_RETURN: { StkId ra = base + a; int n_ = b - 1; if (n_ < 0) n_ = cast_int(L->top.p - ra); L->top.p = ra + n_; ci->u.l.savedpc = (const Instruction *)(f->code + pc + 1); luaD_poscall(L, ci, n_); return 0; }
      case OP_RETURN0: { ci->u.l.savedpc = (const Instruction *)(f->code + pc + 1); L->ci = ci->previous; L->top.p = base - 1; for (int nres = ci->nresults; nres > 0; nres--) setnilvalue(s2v(L->top.p++)); return 0; }
      case OP_RETURN1: { int nres = ci->nresults; ci->u.l.savedpc = (const Instruction *)(f->code + pc + 1); L->ci = ci->previous; if (!nres) L->top.p = base - 1; else { setobjs2s(L, base - 1, base + a); L->top.p = base; for (; nres > 1; nres--) setnilvalue(s2v(L->top.p++)); } return 0; }
      case OP_FORLOOP: {
        StkId ra = base + a;
        if (ttisinteger(s2v(ra + 2))) {
          lua_Unsigned count = l_castS2U(ivalue(s2v(ra + 1)));
          if (count > 0) {
            lua_Integer step = ivalue(s2v(ra + 2));
            lua_Integer idx = ivalue(s2v(ra));
            chgivalue(s2v(ra + 1), count - 1);
            idx = intop(+, idx, step);
            chgivalue(s2v(ra), idx);
            setivalue(s2v(ra + 3), idx);
            pc -= bx;
            continue;
          }
        }
        else if (floatforloop(L, ra)) {
          pc -= bx;
          continue;
        }
        break;
      }
      case OP_FORPREP: {
        StkId ra = base + a;
        ci->u.l.savedpc = (const Instruction *)(f->code + pc);
        if (forprep(L, ra))
          pc += bx;
        break;
      }
      case OP_TFORPREP: {
        StkId ra = base + a;
        /* implicit pairs */
        if (ttistable(s2v(ra))
          && l_likely(!fasttm(L, hvalue(s2v(ra))->metatable, TM_CALL))
        ) {
          setobjs2s(L, ra + 1, ra);
          setfvalue(s2v(ra), luaB_next);
        }
        ci->u.l.savedpc = (const Instruction *)(f->code + pc);
        luaF_newtbcupval(L, ra + 3);
        pc += bx;
        break;
      }
      case OP_TFORCALL: {
        StkId ra = base + a;
        memcpy(ra + 4, ra, 3 * sizeof(*ra));
        L->top.p = ra + 4 + 3;
        ci->u.l.savedpc = (const Instruction *)(f->code + pc + 1);
        luaD_call(L, ra + 4, c);
        if (l_unlikely(ci->u.l.trap)) {
          /* hooks or yield might have happened */
          base = ci->func.p + 1;
        }
        break;
      }
      case OP_TFORLOOP: {
        StkId ra = base + a;
        if (!ttisnil(s2v(ra + 4))) {
          setobjs2s(L, ra + 2, ra + 4);
          pc -= bx;
          continue;
        }
        break;
      }
      case OP_SETLIST: {
        StkId ra = base + a;
        int n = b;
        unsigned int last = c;
        Table *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_int(L->top.p - ra) - 1;
        else
          L->top.p = ci->top.p;
        last += n;
        if (flags) {
          pc++;
          if (pc < f->sizecode)
             last += GETARG_Ax(f->code[pc]) * (MAXARG_C + 1);
        }
        if (last > luaH_realasize(h))
          luaH_resizearray(L, h, last);
        for (; n > 0; n--) {
          TValue *val = s2v(ra + n);
          setobj2t(L, &h->array[last - 1], val);
          last--;
          luaC_barrierback(L, obj2gco(h), val);
        }
        break;
      }
      case OP_CLOSURE: { if (bx >= 0 && bx < f->sizep) { Proto *p_ = f->p[bx]; LClosure *ncl = luaF_newLclosure(L, p_->sizeupvalues); ncl->p = p_; setclLvalue2s(L, base + a, ncl); for (int i = 0; i < p_->sizeupvalues; i++) { if (p_->upvalues[i].instack) ncl->upvals[i] = luaF_findupval(L, base + p_->upvalues[i].idx); else ncl->upvals[i] = cl->upvals[p_->upvalues[i].idx]; } } break; }
      default: { ci->u.l.savedpc = (const Instruction *)(f->code + pc); return 1; }
    }
    pc++;
  }
  return 0;
}


/*
** 对函数进行VM保护
** @param L Lua状态
** @param f 要保护的函数原型
** @param seed 随机种子
** @return 成功返回0，失败返回错误码
**
** 功能描述：
** 1. 初始化VM保护上下文
** 2. 将Lua字节码转换为VM指令
** 3. 加密VM指令
** 4. 生成VM解释器代码
** 5. 更新函数原型
*/
int luaO_vmProtect (lua_State *L, Proto *f, unsigned int seed) {
  fprintf(stderr, "[VM DEBUG] luaO_vmProtect called, sizecode=%d\n", f->sizecode);
  fflush(stderr);
  
  CFF_LOG("========== 开始VM保护 ==========");
  CFF_LOG("函数: sizecode=%d, maxstack=%d", f->sizecode, f->maxstacksize);
  
  /* 代码太短不值得保护 */
  if (f->sizecode < 4) {
    CFF_LOG("代码太短 (%d 条指令)，跳过VM保护", f->sizecode);
    return 0;
  }
  
  fprintf(stderr, "[VM DEBUG] Initializing VM context...\n");
  fflush(stderr);
  
  /* 初始化VM上下文 */
  VMProtectContext *ctx = luaO_initVMContext(L, f, seed);
  if (ctx == NULL) {
    CFF_LOG("初始化VM上下文失败");
    return -1;
  }
  
  fprintf(stderr, "[VM DEBUG] Converting to VM instructions...\n");
  fflush(stderr);
  
  /* 转换Lua字节码到VM指令（生成加密的VM指令数据） */
  if (luaO_convertToVM(ctx) != 0) {
    CFF_LOG("转换VM指令失败");
    luaO_freeVMContext(ctx);
    return -1;
  }
  
  fprintf(stderr, "[VM DEBUG] Setting VM protect flag...\n");
  fflush(stderr);
  
  /* 
  ** 注册VM代码到全局表，供运行时VM解释器使用。
  ** VM指令数据已经生成并加密，存储在ctx->vm_code中。
  */
  
  fprintf(stderr, "[VM DEBUG] Registering VM code to global table...\n");
  fflush(stderr);
  
  /* 注册VM代码到全局表 */
  VMCodeTable *vt = luaO_registerVMCode(L, f, 
                                        ctx->vm_code, 
                                        ctx->vm_code_size,
                                        ctx->encrypt_key,
                                        ctx->reverse_map,
                                        seed);
  if (vt == NULL) {
    CFF_LOG("注册VM代码失败");
    fprintf(stderr, "[VM DEBUG] Failed to register VM code\n");
    fflush(stderr);
    luaO_freeVMContext(ctx);
    return -1;
  }
  
  /* 标记为VM保护 */
  f->difierline_mode |= OBFUSCATE_VM_PROTECT;
  
  /* 存储VM元数据（加密密钥低32位） */
  f->difierline_data = (f->difierline_data & 0xFFFFFFFF00000000ULL) | 
                       (ctx->encrypt_key & 0xFFFFFFFF);
  
  fprintf(stderr, "[VM DEBUG] VM protection complete, vm_code_size=%d\n", ctx->vm_code_size);
  fflush(stderr);
  
  CFF_LOG("========== VM保护完成 ==========");
  CFF_LOG("VM指令数: %d, 加密密钥: 0x%016llx", 
          ctx->vm_code_size, (unsigned long long)ctx->encrypt_key);
  CFF_LOG("VM代码表已注册: proto=%p, vt=%p", (void*)f, (void*)vt);
  
  /* 注意：不释放ctx->vm_code和ctx->reverse_map，因为已转移给VMCodeTable */
  ctx->vm_code = NULL;
  ctx->reverse_map = NULL;
  luaO_freeVMContext(ctx);
  
  fprintf(stderr, "[VM DEBUG] luaO_vmProtect returning 0\n");
  fflush(stderr);
  
  return 0;
}
