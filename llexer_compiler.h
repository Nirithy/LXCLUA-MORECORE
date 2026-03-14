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
    IR_STMT_REPEAT,
    IR_STMT_FOR,
    IR_STMT_FOR_IN,
    IR_STMT_RETURN,
    IR_STMT_FUNCTION,
    IR_STMT_BREAK,
    IR_STMT_EXPR,

    /* Modern Control Flow & Declarations */
    IR_STMT_SWITCH,
    IR_STMT_CASE,
    IR_STMT_DEFAULT,
    IR_STMT_CONTINUE,
    IR_STMT_GOTO,
    IR_STMT_LABEL,
    IR_STMT_TRY,
    IR_STMT_CATCH,
    IR_STMT_FINALLY,
    IR_STMT_DEFER,
    IR_STMT_CLASS,
    IR_STMT_STRUCT,
    IR_STMT_SUPERSTRUCT,
    IR_STMT_ENUM,
    IR_STMT_NAMESPACE,
    IR_STMT_COMPOUND_ASSIGN,

    IR_EXPR_BINOP,
    IR_EXPR_UNOP,
    IR_EXPR_NAME,
    IR_EXPR_LITERAL_NIL,
    IR_EXPR_LITERAL_INT,
    IR_EXPR_LITERAL_FLT,
    IR_EXPR_LITERAL_STR,
    IR_EXPR_LITERAL_BOOL,
    IR_EXPR_TABLE,
    IR_EXPR_CALL,
    IR_EXPR_INDEX,
    IR_EXPR_METHOD,

    /* Modern Expressions */
    IR_EXPR_ARROW_FUNC,
    IR_EXPR_LAMBDA,
    IR_EXPR_ASYNC_FUNC,
    IR_EXPR_AWAIT,
    IR_EXPR_TERNARY,
    IR_EXPR_OPTCHAIN,
    IR_EXPR_NULLCOAL,
    IR_EXPR_COMPREHENSION,
    IR_EXPR_SPREAD,
    IR_EXPR_DESTRUCT_ASSIGN,
    IR_EXPR_PIPE
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
