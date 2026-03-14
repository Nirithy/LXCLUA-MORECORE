/*
** $Id: lir.h $
** SSA Intermediate Representation (IR) Data Structures for LXCLUA-NCore
**
** This IR is designed as a decoupling layer between the AST and the Bytecode,
** allowing advanced optimizations (Constant Folding, Dead Code Elimination)
** and high-level obfuscation (Control Flow Flattening on IR level).
*/

#ifndef lir_h
#define lir_h

#include "lua.h"
#include "lobject.h"
#include "lopcodes.h"

/* Optimization modes / Pipeline Pragma */
typedef enum {
    LIR_OPT_NONE = 0,
    LIR_OPT_SPEED = 1,     /* Route to Fast Dispatcher / JIT / AOT */
    LIR_OPT_OBFUSCATE = 2  /* Route to Secure Dispatcher / VMP */
} LIROptMode;

/* IR OpCodes (Subset/Abstracted version of Lua OpCodes for SSA) */
typedef enum {
    /* Arithmetic / Logic */
    LIR_ADD, LIR_SUB, LIR_MUL, LIR_DIV, LIR_IDIV,
    LIR_MOD, LIR_POW, LIR_BAND, LIR_BOR, LIR_BXOR,
    LIR_SHL, LIR_SHR, LIR_UNM, LIR_BNOT, LIR_NOT,

    /* Memory / Assignment */
    LIR_MOVE, LIR_LOADK, LIR_LOADI, LIR_LOADF, LIR_LOADNIL,
    LIR_LOADBOOL, LIR_NEWTABLE, LIR_GETTABLE, LIR_SETTABLE,
    LIR_GETGLOBAL, LIR_SETGLOBAL, LIR_GETUPVAL, LIR_SETUPVAL,

    /* Control Flow */
    LIR_JMP, LIR_CJMP, /* Conditional JMP */
    LIR_CALL, LIR_RET,

    /* Comparison */
    LIR_EQ, LIR_LT, LIR_LE, LIR_NE, LIR_GT, LIR_GE,

    /* Special / VMP */
    LIR_NOP, LIR_PHI
} LIROpCode;

/* Value Types in SSA Form */
typedef enum {
    LIR_VAL_VREG,    /* Virtual Register (v1, v2, etc.) */
    LIR_VAL_CONST,   /* Constant from K table */
    LIR_VAL_INT,     /* Immediate Integer */
    LIR_VAL_FLOAT,   /* Immediate Float */
    LIR_VAL_UPVAL,   /* Upvalue */
    LIR_VAL_GLOBAL,  /* Global Variable */
    LIR_VAL_LABEL    /* Basic Block ID (for jumps) */
} LIRValType;

/* IR Operand */
typedef struct LIRValue {
    LIRValType type;
    union {
        int vreg;           /* Virtual register number */
        int k_idx;          /* Constant index */
        lua_Integer i;      /* Integer immediate */
        lua_Number n;       /* Float immediate */
        int upval;          /* Upvalue index */
        TString *name;      /* Global variable name */
        int block_id;       /* Target block ID for branch */
    } u;
} LIRValue;

/* IR Instruction */
typedef struct LIRInst {
    LIROpCode op;
    LIRValue dest;        /* Destination register (optional) */
    LIRValue args[3];     /* Up to 3 arguments (e.g. A = B op C) */
    int num_args;
    struct LIRInst *prev; /* Doubly linked list */
    struct LIRInst *next;
} LIRInst;

/* IR Basic Block */
typedef struct LIRBlock {
    int id;               /* Block identifier */
    LIRInst *first;       /* First instruction in block */
    LIRInst *last;        /* Last instruction in block */

    struct LIRBlock **preds;  /* Predecessor blocks */
    int num_preds;
    int capacity_preds;

    struct LIRBlock **succs;  /* Successor blocks */
    int num_succs;
    int capacity_succs;
} LIRBlock;

/* IR Function Context */
typedef struct LIRFunc {
    lua_State *L;
    Proto *p;             /* Related Lua Prototype */

    LIRBlock **blocks;    /* Array of all basic blocks */
    int num_blocks;
    int capacity_blocks;

    LIRBlock *entry;      /* Entry block */
    LIRBlock *exit;       /* Exit block (sink) */

    int next_vreg;        /* Next available virtual register */
    int next_block_id;    /* Next available block ID */

    LIROptMode opt_mode;  /* Optimization pragma */
} LIRFunc;

/* Public API */

/* Memory & Object Creation */
LIRFunc *lir_new_func(lua_State *L, Proto *p);
void lir_free_func(LIRFunc *f);

LIRBlock *lir_new_block(LIRFunc *f);
void lir_add_edge(LIRFunc *f, LIRBlock *from, LIRBlock *to);

LIRInst *lir_emit(LIRFunc *f, LIRBlock *b, LIROpCode op);

#endif
