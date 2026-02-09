/*
** $Id: llex.h $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include <limits.h>

#include "lobject.h"
#include "lzio.h"


/*
** Single-char tokens (terminal symbols) are represented by their own
** numeric code. Other tokens start at the following value.
*/
#define FIRST_RESERVED	(UCHAR_MAX + 1)


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_ASM, TK_BREAK, TK_CASE, TK_CATCH, TK_COMMAND, TK_CONST, TK_CONTINUE, TK_DEFAULT,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_ENUM, TK_EXPORT, TK_FALSE, TK_FINALLY, TK_FOR, TK_FUNCTION,
  TK_GLOBAL, TK_GOTO, TK_IF, TK_IN, TK_IS, TK_KEYWORD, TK_LAMBDA, TK_LOCAL, TK_NIL, TK_NOT, TK_OPERATOR, TK_OR,
  TK_REPEAT,
  TK_RETURN, TK_SWITCH, TK_TAKE, TK_THEN, TK_TRUE, TK_TRY, TK_UNTIL, TK_WHEN, TK_WHILE, TK_WITH,

  /* other terminal symbols */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR, TK_PIPE, TK_REVPIPE, TK_SAFEPIPE,
  TK_DBCOLON, TK_EOS,
  TK_LET, TK_MEAN, TK_WALRUS, TK_ARROW,
  /* 复合赋值运算符 */
  TK_ADDEQ,     /* += */
  TK_SUBEQ,     /* -= */
  TK_MULEQ,     /* *= */
  TK_DIVEQ,     /* /= */
  TK_IDIVEQ,    /* //= */
  TK_MODEQ,     /* %= */
  TK_BANDEQ,    /* &= */
  TK_BOREQ,     /* |= */
  TK_BXOREQ,    /* ~= 作为位异或赋值（在赋值上下文中） */
  TK_SHREQ,     /* >>= */
  TK_SHLEQ,     /* <<= */
  TK_CONCATEQ,  /* ..= */
  TK_PLUSPLUS,  /* ++ 自增运算符 */
  TK_OPTCHAIN,  /* ?. 可选链运算符 */
  TK_NULLCOAL,  /* ?? 空值合并运算符 */
  TK_SPACESHIP, /* <=> 三路比较运算符 */
  TK_DOLLAR,    /* $ 宏调用前缀 */
  TK_DOLLDOLL,  /* $$ 运算符调用前缀 */
  TK_FLT, TK_INT, TK_NAME, TK_STRING, TK_INTERPSTRING, TK_RAWSTRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast_int(TK_WITH-FIRST_RESERVED + 1))


/* Pluto Warning System */
typedef enum {
  WT_ALL = 0,
  WT_VAR_SHADOW,
  WT_GLOBAL_SHADOW,
  WT_TYPE_MISMATCH,
  WT_UNREACHABLE_CODE,
  WT_EXCESSIVE_ARGUMENTS,
  WT_BAD_PRACTICE,
  WT_POSSIBLE_TYPO,
  WT_NON_PORTABLE_CODE,
  WT_NON_PORTABLE_BYTECODE,
  WT_NON_PORTABLE_NAME,
  WT_IMPLICIT_GLOBAL,
  WT_UNANNOTATED_FALLTHROUGH,
  WT_DISCARDED_RETURN,
  WT_FIELD_SHADOW,
  WT_UNUSED_VAR,
  WT_COUNT
} WarningType;

typedef enum {
  WS_OFF,
  WS_ON,
  WS_ERROR
} WarningState;

typedef struct {
  WarningState states[WT_COUNT];
} WarningConfig;


typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


typedef struct Alias {
  TString *name;
  Token *tokens;
  int ntokens;
  struct Alias *next;
} Alias;

typedef struct IncludeState {
  ZIO *z;
  Mbuffer *buff;
  int linenumber;
  int lastline;
  TString *source;
  struct IncludeState *prev;
} IncludeState;


/* state of the lexer plus state of the parser when shared by all
   functions */
typedef struct LexState {

  int lasttoken;
  int curpos;
  int tokpos;
  int current;  /* current character (charint) */
  int linenumber;  /* input line counter */
  int lastline;  /* line of last token 'consumed' */
  Token t;  /* current token */
  Token lookahead;  /* look ahead token */
  struct FuncState *fs;  /* current function (parser) */
  struct lua_State *L;
  ZIO *z;  /* input stream */
  Mbuffer *lastbuff;
  Mbuffer *buff;  /* buffer for tokens */
  Table *h;  /* to avoid collection/reuse strings */
  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  TString *source;  /* current source name */
  TString *envn;  /* environment variable name */

  /* Preprocessor additions */
  Alias *aliases;
  IncludeState *inc_stack;
  Token *pending_tokens; /* For alias expansion */
  int npending;
  int pending_idx;
  Table *defines; /* Compile-time constants */

  /* Warnings */
  WarningConfig warnings;
  int disable_warnings_next_line;

  /* Expression parsing flags */
  int expr_flags;
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_warning (LexState *ls, const char *msg, WarningType wt);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC int luaX_lookahead (LexState *ls);
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
