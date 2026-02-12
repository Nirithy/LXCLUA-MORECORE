/**
 * @file lobfuscate.h
 * @brief Control Flow Flattening Obfuscation for Lua bytecode.
 * 
 * This module provides various obfuscation techniques for Lua bytecode,
 * including control flow flattening, block shuffling, bogus block insertion,
 * and VM protection.
 */

#ifndef lobfuscate_h
#define lobfuscate_h

#include "lobject.h"

#define VM_MAP_SIZE 256
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


/** @name Obfuscation Mode Flags */
/**@{*/
#define OBFUSCATE_NONE              0       /**< No obfuscation. */
#define OBFUSCATE_CFF               (1<<0)  /**< Control flow flattening. */
#define OBFUSCATE_BLOCK_SHUFFLE     (1<<1)  /**< Randomize basic block order. */
#define OBFUSCATE_BOGUS_BLOCKS      (1<<2)  /**< Insert bogus basic blocks. */
#define OBFUSCATE_STATE_ENCODE      (1<<3)  /**< Obfuscate state variable values. */
#define OBFUSCATE_NESTED_DISPATCHER (1<<4)  /**< Multi-layered dispatcher. */
#define OBFUSCATE_OPAQUE_PREDICATES (1<<5)  /**< Opaque predicates (always true/false conditions). */
#define OBFUSCATE_FUNC_INTERLEAVE   (1<<6)  /**< Function interleaving (fake function paths). */
#define OBFUSCATE_VM_PROTECT        (1<<7)  /**< VM protection (custom instruction set). */
#define OBFUSCATE_BINARY_DISPATCHER (1<<8)  /**< Binary search dispatcher. */
#define OBFUSCATE_RANDOM_NOP        (1<<9)  /**< Insert random NOP instructions. */
#define OBFUSCATE_STR_ENCRYPT       (1<<11) /**< String constant encryption. */
/**@}*/


/**
 * @brief Basic block structure.
 * Represents a contiguous sequence of instructions with a single entry and exit.
 */
typedef struct BasicBlock {
  int start_pc;           /**< Start PC of the block. */
  int end_pc;             /**< End PC of the block (exclusive). */
  int state_id;           /**< Assigned state ID for the dispatcher. */
  int original_target;    /**< Original jump target block index. */
  int fall_through;       /**< Next sequential block index (-1 if none). */
  int cond_target;        /**< Conditional jump target block index (-1 if none). */
  int is_entry;           /**< True if this is the function entry block. */
  int is_exit;            /**< True if this is an exit block (contains RETURN). */
} BasicBlock;


/**
 * @brief Control Flow Flattening context.
 * Stores all state information during the flattening process.
 */
typedef struct CFFContext {
  lua_State *L;             /**< Lua state. */
  Proto *f;                 /**< Original function prototype. */
  BasicBlock *blocks;       /**< Array of basic blocks. */
  int num_blocks;           /**< Number of basic blocks. */
  int block_capacity;       /**< Capacity of the blocks array. */
  Instruction *new_code;    /**< Newly generated instruction array. */
  int new_code_size;        /**< Size of the new code. */
  int new_code_capacity;    /**< Capacity of the new code array. */
  int state_reg;            /**< Inner state variable register. */
  int outer_state_reg;      /**< Outer state variable register (nested mode). */
  int opaque_reg1;          /**< Temporary register for opaque predicates 1. */
  int opaque_reg2;          /**< Temporary register for opaque predicates 2. */
  int func_id_reg;          /**< Register for function ID (interleaving mode). */
  int dispatcher_pc;        /**< PC of the main dispatcher loop. */
  int outer_dispatcher_pc;  /**< PC of the outer dispatcher loop (nested mode). */
  int num_groups;           /**< Number of block groups (nested mode). */
  int *group_starts;        /**< Start index of each group (nested mode). */
  int num_fake_funcs;       /**< Number of fake functions (interleaving mode). */
  unsigned int seed;        /**< Random seed. */
  int obfuscate_flags;      /**< Obfuscation flags. */
  int skip_pc0;             /**< True if PC 0 should be skipped during block emission. */
} CFFContext;


/**
 * @brief Flattening metadata.
 * Information required to de-flatten or interpret the obfuscated code.
 */
typedef struct CFFMetadata {
  int enabled;              /**< True if flattening is enabled. */
  int num_blocks;           /**< Number of basic blocks. */
  int state_reg;            /**< State variable register. */
  int dispatcher_pc;        /**< Dispatcher PC. */
  int *block_mapping;       /**< Mapping from state ID to original PC. */
  int original_size;        /**< Original code size. */
  unsigned int seed;        /**< Random seed used for generation. */
} CFFMetadata;


/*
** =======================================================
** Public API
** =======================================================
*/


/**
 * @brief Applies control flow flattening to a function prototype.
 * 
 * @param L Lua state.
 * @param f Function prototype to obfuscate.
 * @param flags Combination of obfuscation mode flags.
 * @param seed Random seed for repeatable results.
 * @param log_path Optional file path to write transformation logs (NULL to disable).
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_flatten (lua_State *L, Proto *f, int flags, unsigned int seed,
                            const char *log_path);


/**
 * @brief Reverses control flow flattening.
 * 
 * @param L Lua state.
 * @param f Obfuscated function prototype.
 * @param metadata Flattening metadata.
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_unflatten (lua_State *L, Proto *f, CFFMetadata *metadata);


/**
 * @brief Serializes flattening metadata for storage.
 * 
 * @param L Lua state.
 * @param ctx CFF context.
 * @param buffer Output buffer.
 * @param size Pointer to buffer size (updated on return).
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_serializeMetadata (lua_State *L, CFFContext *ctx, 
                                       void *buffer, size_t *size);


/**
 * @brief Deserializes flattening metadata.
 * 
 * @param L Lua state.
 * @param buffer Input buffer.
 * @param size Buffer size.
 * @param metadata Output metadata structure.
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_deserializeMetadata (lua_State *L, const void *buffer, 
                                         size_t size, CFFMetadata *metadata);


/**
 * @brief Frees allocated metadata.
 * @param L Lua state.
 * @param metadata Pointer to metadata structure.
 */
LUAI_FUNC void luaO_freeMetadata (lua_State *L, CFFMetadata *metadata);


/*
** =======================================================
** Internal Helper Functions
** =======================================================
*/


/**
 * @brief Identifies basic blocks in the function code.
 * @param ctx CFF context.
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_identifyBlocks (CFFContext *ctx);


/**
 * @brief Checks if an opcode terminates a basic block.
 * @param op Opcode to check.
 * @return 1 if terminator, 0 otherwise.
 */
LUAI_FUNC int luaO_isBlockTerminator (OpCode op);


/**
 * @brief Checks if an opcode is a jump instruction.
 * @param op Opcode to check.
 * @return 1 if jump, 0 otherwise.
 */
LUAI_FUNC int luaO_isJumpInstruction (OpCode op);


/**
 * @brief Gets the target PC of a jump instruction.
 * @param inst The instruction.
 * @param pc Current PC.
 * @return Target PC.
 */
LUAI_FUNC int luaO_getJumpTarget (Instruction inst, int pc);


/**
 * @brief Generates the dispatcher code.
 * @param ctx CFF context.
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_generateDispatcher (CFFContext *ctx);


/**
 * @brief Shuffles basic block order.
 * @param ctx CFF context.
 */
LUAI_FUNC void luaO_shuffleBlocks (CFFContext *ctx);


/**
 * @brief Encodes a state value.
 * @param state Original state ID.
 * @param seed Random seed.
 * @return Encoded state value.
 */
LUAI_FUNC int luaO_encodeState (int state, unsigned int seed);


/**
 * @brief Decodes an encoded state value.
 * @param encoded_state Encoded value.
 * @param seed Random seed.
 * @return Original state ID.
 */
LUAI_FUNC int luaO_decodeState (int encoded_state, unsigned int seed);


/**
 * @brief Creates a NOP instruction with fake parameters.
 * @param seed Random seed.
 * @return Generated instruction.
 */
LUAI_FUNC Instruction luaO_createNOP (unsigned int seed);


/**
 * @brief Generates a nested dispatcher (multi-layered state machine).
 * @param ctx CFF context.
 * @return 0 on success, error code on failure.
 */
LUAI_FUNC int luaO_generateNestedDispatcher (CFFContext *ctx);


/** @brief Opaque predicate type. */
typedef enum {
  OP_ALWAYS_TRUE,     /**< Always true. */
  OP_ALWAYS_FALSE     /**< Always false. */
} OpaquePredicateType;


/**
 * @brief Emits an opaque predicate into the code.
 * @param ctx CFF context.
 * @param type Predicate type.
 * @param seed Random seed pointer (updated).
 * @return Number of instructions emitted, or -1 on failure.
 */
LUAI_FUNC int luaO_emitOpaquePredicate (CFFContext *ctx, OpaquePredicateType type,
                                         unsigned int *seed);


/*
** =======================================================
** VM Protection
** =======================================================
*/


/**
 * @brief Custom VM opcodes.
 */
typedef enum {
  VM_OP_NOP = 0,
  VM_OP_LOAD,
  VM_OP_MOVE,
  VM_OP_STORE,
  VM_OP_ADD,
  VM_OP_SUB,
  VM_OP_MUL,
  VM_OP_DIV,
  VM_OP_MOD,
  VM_OP_POW,
  VM_OP_UNM,
  VM_OP_IDIV,
  VM_OP_BAND,
  VM_OP_BOR,
  VM_OP_BXOR,
  VM_OP_BNOT,
  VM_OP_SHL,
  VM_OP_SHR,
  VM_OP_JMP,
  VM_OP_JEQ,
  VM_OP_JNE,
  VM_OP_JLT,
  VM_OP_JLE,
  VM_OP_JGT,
  VM_OP_JGE,
  VM_OP_CALL,
  VM_OP_RET,
  VM_OP_TAILCALL,
  VM_OP_NEWTABLE,
  VM_OP_GETTABLE,
  VM_OP_SETTABLE,
  VM_OP_GETFIELD,
  VM_OP_SETFIELD,
  VM_OP_GETI,
  VM_OP_SETI,
  VM_OP_GETTABUP,
  VM_OP_SETTABUP,
  VM_OP_CLOSURE,
  VM_OP_GETUPVAL,
  VM_OP_SETUPVAL,
  VM_OP_CONCAT,
  VM_OP_LEN,
  VM_OP_NOT,
  VM_OP_TEST,
  VM_OP_TESTSET,
  VM_OP_FORLOOP,
  VM_OP_FORPREP,
  VM_OP_TFORPREP,
  VM_OP_TFORCALL,
  VM_OP_TFORLOOP,
  VM_OP_VARARG,
  VM_OP_VARARGPREP,
  VM_OP_SELF,
  VM_OP_SETLIST,
  VM_OP_LOADKX,
  VM_OP_LOADFALSE,
  VM_OP_LOADTRUE,
  VM_OP_LOADNIL,
  VM_OP_MMBIN,
  VM_OP_MMBINI,
  VM_OP_MMBINK,
  VM_OP_EXT1,
  VM_OP_EXT2,
  VM_OP_HALT,
  VM_OP_COUNT
} VMOpCode;


/** @brief VM instruction type (64-bit). */
typedef uint64_t VMInstruction;

/** @name VM Instruction Field Macros */
/**@{*/
#define VM_GET_OP(inst)    ((int)((inst) & 0xFF))
#define VM_GET_A(inst)     ((int)(((inst) >> 8) & 0xFFFF))
#define VM_GET_B(inst)     ((int)(((inst) >> 24) & 0xFFFF))
#define VM_GET_C(inst)     ((int)(((inst) >> 40) & 0xFFFF))
#define VM_GET_Bx(inst)    ((int64_t)(((inst) >> 24) & 0xFFFFFFFFFFULL))
#define VM_GET_FLAGS(inst) ((int)(((inst) >> 56) & 0xFF))
/**@}*/

/** @brief Constructs a VM instruction. */
#define VM_MAKE_INST(op, a, b, c, flags) \
  (((uint64_t)(op) & 0xFF) | \
   (((uint64_t)(a) & 0xFFFF) << 8) | \
   (((uint64_t)(b) & 0xFFFF) << 24) | \
   (((uint64_t)(c) & 0xFFFF) << 40) | \
   (((uint64_t)(flags) & 0xFF) << 56))

#define VM_MAKE_INST_BX(op, a, bx) \
  (((uint64_t)(op) & 0xFF) | \
   (((uint64_t)(a) & 0xFFFF) << 8) | \
   (((uint64_t)(bx) & 0xFFFFFFFFFFULL) << 24))


/** @brief VM Protection context. */
typedef struct VMProtectContext {
  lua_State *L;             /**< Lua state. */
  Proto *f;                 /**< Original prototype. */
  VMInstruction *vm_code;   /**< Encrypted VM instructions. */
  int vm_code_size;         /**< Number of instructions. */
  int vm_code_capacity;     /**< Array capacity. */
  uint64_t encrypt_key;     /**< Encryption key. */
  int *opcode_map;          /**< Opcode mapping table. */
  int *reverse_map;         /**< Reverse mapping table. */
  unsigned int seed;        /**< Random seed. */
} VMProtectContext;


/** @brief VM Runtime state. */
typedef struct VMState {
  VMInstruction *code;      /**< VM code. */
  int code_size;            /**< Code size. */
  int pc;                   /**< Program Counter. */
  uint64_t decrypt_key;     /**< Decryption key. */
  int *opcode_map;          /**< Opcode map. */
  lua_State *L;             /**< Lua state. */
  CallInfo *ci;             /**< Call info. */
} VMState;


/** @brief Node in the global list of VM-protected code tables. */
typedef struct VMCodeTable {
  struct Proto *proto;       /**< Associated prototype. */
  VMInstruction *code;       /**< Encrypted instructions. */
  int size;                  /**< Number of instructions. */
  int capacity;              /**< Array capacity. */
  uint64_t encrypt_key;      /**< Encryption key. */
  int *reverse_map;          /**< Opcode reverse mapping. */
  unsigned int seed;         /**< Random seed. */
  struct VMCodeTable *next;  /**< Next node in list. */
} VMCodeTable;


/** @name VM Protection API */
/**@{*/

/**
 * @brief Registers VM code in the global list.
 */
LUAI_FUNC VMCodeTable *luaO_registerVMCode (lua_State *L, struct Proto *p,
                                            VMInstruction *code, int size,
                                            uint64_t key, int *reverse_map,
                                            unsigned int seed);

/**
 * @brief Finds registered VM code for a prototype.
 */
LUAI_FUNC VMCodeTable *luaO_findVMCode (lua_State *L, struct Proto *p);

/**
 * @brief Frees all registered VM code tables.
 */
LUAI_FUNC void luaO_freeAllVMCode (lua_State *L);

/**
 * @brief Initializes VM protection context.
 */
LUAI_FUNC VMProtectContext *luaO_initVMContext (lua_State *L, Proto *f, 
                                                  unsigned int seed);

/**
 * @brief Frees VM protection context.
 */
LUAI_FUNC void luaO_freeVMContext (VMProtectContext *ctx);

/**
 * @brief Translates Lua bytecode to custom VM instructions.
 */
LUAI_FUNC int luaO_convertToVM (VMProtectContext *ctx);

/**
 * @brief Executes VM-protected code.
 */
LUAI_FUNC int luaO_executeVM (lua_State *L, Proto *f);

/**
 * @brief Applies VM protection to a function prototype.
 */
LUAI_FUNC int luaO_vmProtect (lua_State *L, Proto *f, unsigned int seed);

/**@}*/

#endif /* lobfuscate_h */
