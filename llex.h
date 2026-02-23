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
/**
 * @brief Reserved words and other terminal symbols.
 */
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_ASM, TK_ASYNC, TK_AWAIT, TK_BOOL, TK_BREAK, TK_CASE, TK_CATCH, TK_CHAR, TK_COMMAND, TK_CONCEPT, TK_CONST, TK_CONTINUE, TK_DEFAULT,
  TK_DEFER, TK_DO, TK_DOUBLE, TK_ELSE, TK_ELSEIF, TK_END, TK_ENUM, TK_EXPORT, TK_FALSE, TK_FINALLY, TK_TYPE_FLOAT, TK_FOR, TK_FUNCTION,
  TK_GLOBAL, TK_GOTO, TK_IF, TK_IN, TK_TYPE_INT, TK_IS, TK_KEYWORD, TK_LAMBDA, TK_LOCAL, TK_LONG, TK_NAMESPACE, TK_NIL, TK_NOT, TK_OPERATOR, TK_OR,
  TK_REPEAT, TK_REQUIRES,
  TK_RETURN, TK_STRUCT, TK_SUPERSTRUCT, TK_SWITCH, TK_TAKE, TK_THEN, TK_TRUE, TK_TRY, TK_UNTIL, TK_USING, TK_VOID, TK_WHEN, TK_WHILE, TK_WITH,

  /* other terminal symbols */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR, TK_PIPE, TK_REVPIPE, TK_SAFEPIPE,
  TK_DBCOLON, TK_EOS,
  TK_LET, TK_MEAN, TK_WALRUS, TK_ARROW,
  /* 复合赋值运算符 */
  TK_ADDEQ,     /**< += */
  TK_SUBEQ,     /**< -= */
  TK_MULEQ,     /**< *= */
  TK_DIVEQ,     /**< /= */
  TK_IDIVEQ,    /**< //= */
  TK_MODEQ,     /**< %= */
  TK_BANDEQ,    /**< &= */
  TK_BOREQ,     /**< |= */
  TK_BXOREQ,    /**< ~= */
  TK_SHREQ,     /**< >>= */
  TK_SHLEQ,     /**< <<= */
  TK_CONCATEQ,  /**< ..= */
  TK_PLUSPLUS,  /**< ++ */
  TK_OPTCHAIN,  /**< ?. */
  TK_NULLCOAL,  /**< ?? */
  TK_SPACESHIP, /**< <=> */
  TK_DOLLAR,    /**< $ */
  TK_DOLLDOLL,  /**< $$ */
  TK_FLT, TK_INT, TK_NAME, TK_STRING, TK_INTERPSTRING, TK_RAWSTRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast_int(TK_WITH-FIRST_RESERVED + 1))


/**
 * @brief Warning types.
 */
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

/**
 * @brief Warning states.
 */
typedef enum {
  WS_OFF,
  WS_ON,
  WS_ERROR
} WarningState;

/**
 * @brief Warning configuration.
 */
typedef struct {
  WarningState states[WT_COUNT];
} WarningConfig;


/**
 * @brief Semantic information for a token.
 */
typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


/**
 * @brief Token structure.
 */
typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


/**
 * @brief Alias structure for preprocessor.
 */
typedef struct Alias {
  TString *name;
  Token *tokens;
  int ntokens;
  struct Alias *next;
} Alias;

/**
 * @brief Include state for preprocessor.
 */
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
/**
 * @brief Lexical state structure.
 */
typedef struct LexState {

  int lasttoken;
  int curpos;
  int tokpos;
  int current;  /**< Current character (charint). */
  int linenumber;  /**< Input line counter. */
  int lastline;  /**< Line of last token 'consumed'. */
  Token t;  /**< Current token. */
  Token lookahead;  /**< Look ahead token. */
  Token lookahead2; /**< Second look ahead token. */
  struct FuncState *fs;  /**< Current function (parser). */
  struct lua_State *L;
  ZIO *z;  /**< Input stream. */
  Mbuffer *lastbuff;
  Mbuffer *buff;  /**< Buffer for tokens. */
  Table *h;  /**< To avoid collection/reuse strings. */
  struct Dyndata *dyd;  /**< Dynamic structures used by the parser. */
  TString *source;  /**< Current source name. */
  TString *envn;  /**< Environment variable name. */

  /* Preprocessor additions */
  Alias *aliases;
  IncludeState *inc_stack;
  Token *pending_tokens; /**< For alias expansion. */
  int npending;
  int pending_idx;
  Table *defines; /**< Compile-time constants. */
  Table *named_types; /**< Named types. */
  Table *declared_globals; /**< Declared global variables. */
  struct TypeHint *all_type_hints; /**< List of allocated type hints. */

  /* Warnings */
  WarningConfig warnings;
  int disable_warnings_next_line;

  /* Expression parsing flags */
  int expr_flags;
} LexState;


/**
 * @brief Initializes the lexical analyzer.
 *
 * @param L The Lua state.
 */
LUAI_FUNC void luaX_init (lua_State *L);

/**
 * @brief Reports a warning.
 *
 * @param ls The lexical state.
 * @param msg The warning message.
 * @param wt The warning type.
 */
LUAI_FUNC void luaX_warning (LexState *ls, const char *msg, WarningType wt);

/**
 * @brief Sets the input for the lexical analyzer.
 *
 * @param L The Lua state.
 * @param ls The lexical state.
 * @param z The input stream.
 * @param source The source name.
 * @param firstchar The first character.
 */
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);

/**
 * @brief Creates a new string in the lexical analyzer context.
 *
 * @param ls The lexical state.
 * @param str The string content.
 * @param l The length of the string.
 * @return The new string.
 */
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);

/**
 * @brief Gets the next token.
 *
 * @param ls The lexical state.
 */
LUAI_FUNC void luaX_next (LexState *ls);

/**
 * @brief Looks ahead one token.
 *
 * @param ls The lexical state.
 * @return The token code.
 */
LUAI_FUNC int luaX_lookahead (LexState *ls);

/**
 * @brief Looks ahead two tokens.
 *
 * @param ls The lexical state.
 * @return The token code.
 */
LUAI_FUNC int luaX_lookahead2 (LexState *ls);

/**
 * @brief Reports a syntax error.
 *
 * @param ls The lexical state.
 * @param s The error message.
 */
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);

/**
 * @brief Converts a token to a string.
 *
 * @param ls The lexical state.
 * @param token The token code.
 * @return The string representation.
 */
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
