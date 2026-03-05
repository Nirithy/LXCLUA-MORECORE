/*
** $Id: llexer_compiler.h $
** Lexer-based Compiler Frontend & Obfuscator
*/

#ifndef llexer_compiler_h
#define llexer_compiler_h

#include "lua.h"
#include "llex.h"

/* Basic Node Types for AST */
typedef enum {
    IR_STMT_LIST,
    IR_STMT_LOCAL,
    IR_STMT_ASSIGN,
    IR_STMT_CALL,
    IR_STMT_IF,
    IR_STMT_WHILE,
    IR_STMT_RETURN,
    IR_STMT_FUNCTION,
    IR_STMT_BREAK,

    IR_EXPR_BINOP,
    IR_EXPR_UNOP,
    IR_EXPR_NAME,
    IR_EXPR_LITERAL_INT,
    IR_EXPR_LITERAL_FLT,
    IR_EXPR_LITERAL_STR,
    IR_EXPR_LITERAL_BOOL,
    IR_EXPR_CALL,
    IR_EXPR_INDEX
} IRNodeType;

/* AST Node Structure */
typedef struct IRNode {
    IRNodeType type;
    int token; /* Primary token associated, if any */
    struct IRNode *next; /* For lists/blocks */
    struct IRNode *children[4]; /* Operands/Sub-statements */
    char *str_val; /* For names, strings */
    lua_Integer int_val;
    lua_Number flt_val;
} IRNode;

/* Basic Block for CFG */
typedef struct BasicBlock {
    int id;
    IRNode *stmts; /* Linked list of statements in this block */
    struct BasicBlock *next_true;
    struct BasicBlock *next_false;
    struct BasicBlock *next; /* Sequential fallthrough */
} BasicBlock;

/* Control Flow Graph */
typedef struct CFG {
    BasicBlock *entry;
    int next_block_id;
} CFG;

/* Main Obfuscation Entry Point */
LUAMOD_API int lua_lexer_obfuscate(lua_State *L, const char *code, int cff, int bogus, int str_enc);

#endif
