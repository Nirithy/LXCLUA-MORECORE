/*
** $Id: lparser.c $
** Lua Parser
** See Copyright Notice in lua.h
*/

#define lparser_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <string.h>
#include <stdio.h>

#include "lua.h"

#include "lcode.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lopnames.h"


extern void luaX_pushincludefile(LexState *ls, const char *filename);
extern void luaX_addalias(LexState *ls, TString *name, Token *tokens, int ntokens);


/* maximum number of local variables per function (must be smaller
   than 250, due to the bytecode format) */
#define MAXVARS		200


#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
#define eqstr(a,b)	((a) == (b))


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  int firstlabel;  /* index of first label in this block */
  int firstgoto;  /* index of first pending goto in this block */
  lu_byte nactvar;  /* # active locals outside the block */
  lu_byte upval;  /* true if some variable in the block is an upvalue */
  lu_byte isloop;  /* true if 'block' is a loop */
  lu_byte insidetbc;  /* true if inside the scope of a to-be-closed var. */
  struct {
    TString **arr;
    int n;
    int size;
  } exports;
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void statement (LexState *ls);
static void expr (LexState *ls, expdesc *v);
static void retstat (LexState *ls);
static void breakstat (LexState *ls);
static void buildglobal (LexState *ls, TString *varname, expdesc *var);
static int new_varkind (LexState *ls, TString *name, lu_byte kind);
static void switchstat (LexState *ls, int line);  /* switch语句的前向声明 */
static void trystat (LexState *ls, int line);     /* try语句的前向声明 */
static void withstat (LexState *ls, int line);    /* with语句的前向声明 */
static void classstat (LexState *ls, int line, int class_flags, int isexport);   /* class语句的前向声明 */
static void interfacestat (LexState *ls, int line); /* interface语句的前向声明 */
static void enumstat (LexState *ls, int line, int isexport);    /* enum语句的前向声明 */
static void newexpr (LexState *ls, expdesc *v);   /* onew表达式的前向声明 */
static void superexpr (LexState *ls, expdesc *v); /* osuper表达式的前向声明 */
static void cond_expr (LexState *ls, expdesc *v); /* 条件表达式的前向声明（不将{作为函数调用） */
static void constexprstat (LexState *ls);         /* 预处理语句 */

static l_noret error_expected (LexState *ls, int token) {
  luaX_syntaxerror(ls,
      luaO_pushfstring(ls->L, "%s expected", luaX_token2str(ls, token)));
}

static void breaklvm(LexState *ls);


static l_noret errorlimit (FuncState *fs, int limit, const char *what) {
  lua_State *L = fs->ls->L;
  const char *msg;
  int line = fs->f->linedefined;
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  luaX_syntaxerror(fs->ls, msg);
}


static void checklimit (FuncState *fs, int v, int l, const char *what) {
  if (v > l) errorlimit(fs, l, what);
}


/*
** Test whether next token is 'c'; if so, skip it.
*/
static int testnext (LexState *ls, int c) {
  if (ls->t.token == c) {
    luaX_next(ls);
    return 1;
  }
  else return 0;
}
static int testtoken (LexState *ls, int c) {
  if (ls->t.token == c) {
    return 1;
  }
  else return 0;
}


/*
** =====================================================================
** 软关键字系统 (Soft Keyword System)
** 软关键字是上下文敏感的关键字，只在特定语法位置被识别为关键字
** 在其他位置可以作为普通标识符使用
** 
** 特性：
** - 支持多上下文（使用位掩码）
** - 支持前瞻匹配（后面跟什么时识别为关键字）
** - 支持排除列表（后面跟什么时不识别为关键字）
** - 使用哈希表优化查找效率
** =====================================================================
*/

/*
** 软关键字上下文类型（位掩码，支持组合）
*/
#define SOFTKW_CTX_NONE         0x00  /* 无上下文 */
#define SOFTKW_CTX_STMT_BEGIN   0x01  /* 语句开头（如 class, interface） */
#define SOFTKW_CTX_EXPR         0x02  /* 表达式中（如 new） */
#define SOFTKW_CTX_CLASS_BODY   0x04  /* 类体内部（如 private, protected） */
#define SOFTKW_CTX_CLASS_INHERIT 0x08 /* 类继承上下文（如 extends, implements） */
#define SOFTKW_CTX_ANY          0xFF  /* 任意上下文 */

/*
** 软关键字 ID 枚举
** 用于在解析器中快速判断软关键字类型
*/
typedef enum {
  SKW_NONE = 0,
  /* 类定义相关 */
  SKW_CLASS,
  SKW_INTERFACE,
  SKW_EXTENDS,
  SKW_IMPLEMENTS,
  /* 访问修饰符 */
  SKW_PRIVATE,
  SKW_PROTECTED,
  SKW_PUBLIC,
  SKW_STATIC,
  SKW_ABSTRACT,
  SKW_FINAL,
  SKW_SEALED,    /* 密封类修饰符 */
  /* getter/setter */
  SKW_GET,       /* getter 属性访问器 */
  SKW_SET,       /* setter 属性访问器 */
  /* 表达式相关 */
  SKW_NEW,
  SKW_SUPER,
  /* 总数 */
  SKW_COUNT
} SoftKWID;

/*
** 软关键字定义结构（增强版）
*/
typedef struct {
  const char *name;           /* 关键字名称 */
  SoftKWID id;                /* 关键字 ID */
  unsigned int contexts;      /* 允许的上下文（位掩码） */
  int lookahead_tokens[8];    /* 前瞻匹配列表（以 0 结尾，后面跟这些时识别为关键字） */
  int exclude_tokens[4];      /* 排除列表（以 0 结尾，后面跟这些时不识别为关键字） */
  unsigned int hash;          /* 名称哈希值（运行时计算） */
} SoftKWDef;

/*
** 软关键字定义表
** 按字母顺序排列以便二分查找
*/
static SoftKWDef soft_keywords[] = {
  /* abstract - 语句开头（abstract class）或类体内（abstract function）*/
  {"abstract",   SKW_ABSTRACT,   SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* class - 语句开头，后面必须跟类名 */
  {"class",      SKW_CLASS,      SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* extends - 类继承上下文，后面必须跟类名 */
  {"extends",    SKW_EXTENDS,    SOFTKW_CTX_CLASS_INHERIT, {TK_NAME, 0}, {'=', 0}, 0},
  /* final - 语句开头（final class）或类体内（final function）*/
  {"final",      SKW_FINAL,      SOFTKW_CTX_STMT_BEGIN | SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* get - 类体内，getter属性访问器 */
  {"get",        SKW_GET,        SOFTKW_CTX_CLASS_BODY,    {TK_NAME, 0}, {'=', 0}, 0},
  /* implements - 类继承上下文，后面必须跟接口名 */
  {"implements", SKW_IMPLEMENTS, SOFTKW_CTX_CLASS_INHERIT, {TK_NAME, 0}, {'=', 0}, 0},
  /* ointerface - 语句开头，后面必须跟接口名 */
  {"ointerface", SKW_INTERFACE,  SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* onew - 表达式中，后面必须跟类名 */
  {"onew",       SKW_NEW,        SOFTKW_CTX_EXPR,          {TK_NAME, 0}, {'=', 0}, 0},
  /* osuper - 表达式中，后面必须跟.或: */
  {"osuper",     SKW_SUPER,      SOFTKW_CTX_EXPR,          {'.', ':', 0}, {'=', 0}, 0},
  /* private - 类体内，后面跟function或标识符名 */
  {"private",    SKW_PRIVATE,    SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* protected - 类体内，后面跟function或标识符名 */
  {"protected",  SKW_PROTECTED,  SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* public - 类体内，后面跟function或标识符名 */
  {"public",     SKW_PUBLIC,     SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* sealed - 语句开头（sealed class）密封类 */
  {"sealed",     SKW_SEALED,     SOFTKW_CTX_STMT_BEGIN,    {TK_NAME, 0}, {'=', 0}, 0},
  /* set - 类体内，setter属性访问器 */
  {"set",        SKW_SET,        SOFTKW_CTX_CLASS_BODY,    {TK_NAME, 0}, {'=', 0}, 0},
  /* static - 类体内，后面跟function或标识符名 */
  {"static",     SKW_STATIC,     SOFTKW_CTX_CLASS_BODY,    {TK_FUNCTION, TK_NAME, 0}, {'=', 0}, 0},
  /* 结束标记 */
  {NULL,         SKW_NONE,       0,                         {0}, {0}, 0}
};

/* 哈希表大小（使用质数以减少冲突） */
#define SOFTKW_HASH_SIZE 19

/* 哈希表（存储软关键字定义的指针） */
static SoftKWDef *softkw_hashtable[SOFTKW_HASH_SIZE];

/* 标记哈希表是否已初始化 */
static int softkw_initialized = 0;


/*
** 计算字符串哈希值
** 参数：
**   s - 字符串
** 返回值：
**   哈希值
*/
static unsigned int softkw_hash (const char *s) {
  unsigned int h = 0;
  while (*s) {
    h = h * 31 + (unsigned char)*s++;
  }
  return h;
}


/*
** 初始化软关键字哈希表
** 说明：
**   在第一次使用前自动调用，构建哈希表以加速查找
*/
static void softkw_init (void) {
  if (softkw_initialized) return;
  
  /* 清空哈希表 */
  for (int i = 0; i < SOFTKW_HASH_SIZE; i++) {
    softkw_hashtable[i] = NULL;
  }
  
  /* 计算每个软关键字的哈希值并插入哈希表 */
  for (int i = 0; soft_keywords[i].name != NULL; i++) {
    soft_keywords[i].hash = softkw_hash(soft_keywords[i].name);
    /* 使用开放寻址法处理冲突 */
    unsigned int idx = soft_keywords[i].hash % SOFTKW_HASH_SIZE;
    while (softkw_hashtable[idx] != NULL) {
      idx = (idx + 1) % SOFTKW_HASH_SIZE;
    }
    softkw_hashtable[idx] = &soft_keywords[i];
  }
  
  softkw_initialized = 1;
}


/*
** 根据名称查找软关键字定义（使用哈希表）
** 参数：
**   name - 关键字名称
** 返回值：
**   软关键字定义指针，未找到返回 NULL
*/
static SoftKWDef* softkw_find (const char *name) {
  if (!softkw_initialized) softkw_init();
  
  unsigned int h = softkw_hash(name);
  unsigned int idx = h % SOFTKW_HASH_SIZE;
  int count = 0;
  
  /* 开放寻址法查找 */
  while (softkw_hashtable[idx] != NULL && count < SOFTKW_HASH_SIZE) {
    if (softkw_hashtable[idx]->hash == h && 
        strcmp(softkw_hashtable[idx]->name, name) == 0) {
      return softkw_hashtable[idx];
    }
    idx = (idx + 1) % SOFTKW_HASH_SIZE;
    count++;
  }
  
  return NULL;
}


/*
** 根据 ID 查找软关键字定义
** 参数：
**   id - 软关键字 ID
** 返回值：
**   软关键字定义指针，未找到返回 NULL
*/
static SoftKWDef* softkw_findbyid (SoftKWID id) {
  if (!softkw_initialized) softkw_init();
  
  /* ID查找使用线性搜索（通常用于验证，不频繁调用） */
  for (int i = 0; soft_keywords[i].name != NULL; i++) {
    if (soft_keywords[i].id == id) {
      return &soft_keywords[i];
    }
  }
  return NULL;
}


/*
** 检查前瞻token是否在匹配列表中
** 参数：
**   lookahead - 前瞻token
**   tokens - token列表（以0结尾）
** 返回值：
**   1 - 匹配
**   0 - 不匹配
*/
static int softkw_match_lookahead (int lookahead, const int *tokens) {
  if (tokens[0] == 0) return 1;  /* 空列表表示无条件匹配 */
  for (int i = 0; tokens[i] != 0 && i < 8; i++) {
    if (lookahead == tokens[i]) {
      return 1;
    }
  }
  return 0;
}


/*
** 检查前瞻token是否在排除列表中
** 参数：
**   lookahead - 前瞻token
**   tokens - 排除token列表（以0结尾）
** 返回值：
**   1 - 在排除列表中
**   0 - 不在排除列表中
*/
static int softkw_in_exclude (int lookahead, const int *tokens) {
  for (int i = 0; tokens[i] != 0 && i < 4; i++) {
    if (lookahead == tokens[i]) {
      return 1;
    }
  }
  return 0;
}


/*
** 检查当前 token 是否是指定上下文的软关键字
** 参数：
**   ls - 词法状态
**   context - 上下文类型（位掩码）
** 返回值：
**   软关键字 ID，不匹配返回 SKW_NONE
*/
static SoftKWID softkw_check (LexState *ls, unsigned int context) {
  if (ls->t.token != TK_NAME) {
    return SKW_NONE;
  }
  
  const char *name = getstr(ls->t.seminfo.ts);
  SoftKWDef *def = softkw_find(name);
  
  if (def == NULL) {
    return SKW_NONE;
  }
  
  /* 检查上下文是否匹配（使用位掩码） */
  if ((def->contexts & context) == 0) {
    return SKW_NONE;
  }
  
  /* 获取前瞻token（优先使用已缓存的lookahead，避免重复调用luaX_lookahead） */
  int lookahead;
  if (ls->lookahead.token != TK_EOS) {
    lookahead = ls->lookahead.token;
  } else {
    lookahead = luaX_lookahead(ls);
  }
  
  /* 检查排除列表 */
  if (softkw_in_exclude(lookahead, def->exclude_tokens)) {
    return SKW_NONE;  /* 后面跟的是排除的token，当作普通标识符 */
  }
  
  /* 检查前瞻匹配列表 */
  if (!softkw_match_lookahead(lookahead, def->lookahead_tokens)) {
    return SKW_NONE;  /* 前瞻不匹配，当作普通标识符 */
  }
  
  return def->id;
}


/*
** 检查当前 token 是否是指定上下文的软关键字，如果是则消耗它
** 参数：
**   ls - 词法状态
**   context - 上下文类型（位掩码）
** 返回值：
**   软关键字 ID，不匹配返回 SKW_NONE（不消耗 token）
*/
static SoftKWID softkw_checknext (LexState *ls, unsigned int context) {
  SoftKWID id = softkw_check(ls, context);
  if (id != SKW_NONE) {
    luaX_next(ls);
  }
  return id;
}


/*
** 检查当前 token 是否是指定 ID 的软关键字
** 参数：
**   ls - 词法状态
**   id - 软关键字 ID
**   context - 上下文类型（位掩码），传入 0 表示不检查上下文
** 返回值：
**   1 - 是指定的软关键字
**   0 - 不是
*/
static int softkw_test (LexState *ls, SoftKWID id, unsigned int context) {
  if (ls->t.token != TK_NAME) {
    return 0;
  }
  
  SoftKWDef *def = softkw_findbyid(id);
  if (def == NULL) {
    return 0;
  }
  
  const char *name = getstr(ls->t.seminfo.ts);
  if (strcmp(name, def->name) != 0) {
    return 0;
  }
  
  /* 检查上下文是否匹配（如果指定了上下文） */
  if (context != 0 && (def->contexts & context) == 0) {
    return 0;
  }
  
  /* 获取前瞻token（优先使用已缓存的lookahead，避免重复调用luaX_lookahead） */
  int lookahead;
  if (ls->lookahead.token != TK_EOS) {
    lookahead = ls->lookahead.token;
  } else {
    lookahead = luaX_lookahead(ls);
  }
  
  /* 检查排除列表 */
  if (softkw_in_exclude(lookahead, def->exclude_tokens)) {
    return 0;
  }
  
  /* 检查前瞻匹配列表 */
  if (!softkw_match_lookahead(lookahead, def->lookahead_tokens)) {
    return 0;
  }
  
  return 1;
}


/*
** 检查当前 token 是否是指定 ID 的软关键字，如果是则消耗它
** 参数：
**   ls - 词法状态
**   id - 软关键字 ID
**   context - 上下文类型（位掩码），传入 0 表示不检查上下文
** 返回值：
**   1 - 是指定的软关键字（已消耗）
**   0 - 不是（未消耗）
*/
static int softkw_testnext (LexState *ls, SoftKWID id, unsigned int context) {
  if (softkw_test(ls, id, context)) {
    luaX_next(ls);
    return 1;
  }
  return 0;
}


/*
** Check that next token is 'c'.
*/
static void check (LexState *ls, int c) {
  if (ls->t.token != c)
    error_expected(ls, c);
}


/*
** Check that next token is 'c' and skip it.
*/
static void checknext (LexState *ls, int c) {
  check(ls, c);
  luaX_next(ls);
}


#define check_condition(ls,c,msg)	{ if (!(c)) luaX_syntaxerror(ls, msg); }


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
static void check_match (LexState *ls, int what, int who, int where) {
  if (l_unlikely(!testnext(ls, what))) {
    if (where == ls->linenumber)  /* all in the same line? */
      error_expected(ls, what);  /* do not need a complex message */
    else {
      luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
             "%s expected (to close %s at line %d)",
              luaX_token2str(ls, what), luaX_token2str(ls, who), where));
    }
  }
}


static TString *str_checkname (LexState *ls) {
  TString *ts;
  check(ls, TK_NAME);
  ts = ls->t.seminfo.ts;
  luaX_next(ls);
  return ts;
}


static void init_exp (expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
}


static void codestring (expdesc *e, TString *s) {
  e->f = e->t = NO_JUMP;
  e->k = VKSTR;
  e->u.strval = s;
}


static void codename (LexState *ls, expdesc *e) {
  codestring(e, str_checkname(ls));
}


/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
static int registerlocalvar (LexState *ls, FuncState *fs, TString *varname) {
  Proto *f = fs->f;
  int oldsize = f->sizelocvars;
  luaM_growvector(ls->L, f->locvars, fs->ndebugvars, f->sizelocvars,
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < f->sizelocvars)
    f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->ndebugvars].varname = varname;
  f->locvars[fs->ndebugvars].startpc = fs->pc;
  luaC_objbarrier(ls->L, f, varname);
  return fs->ndebugvars++;
}


/*
** Create a new local variable with the given 'name'. Return its index
** in the function.
*/
static int new_localvar (LexState *ls, TString *name) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  Vardesc *var;
  checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
                 MAXVARS, "local variables");
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = VDKREG;  /* default */
  var->vd.name = name;
  return dyd->actvar.n - 1 - fs->firstlocal;
}

#define new_localvarliteral(ls,v) \
    new_localvar(ls,  \
      luaX_newstring(ls, "" v, (sizeof(v)/sizeof(char)) - 1));



/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
static Vardesc *getlocalvardesc (FuncState *fs, int vidx) {
  return &fs->ls->dyd->actvar.arr[fs->firstlocal + vidx];
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** register. For that, search for the highest variable below that level
** that is in a register and uses its register index ('ridx') plus one.
*/
static int reglevel (FuncState *fs, int nvar) {
  while (nvar-- > 0) {
    Vardesc *vd = getlocalvardesc(fs, nvar);  /* get previous variable */
    if (vd->vd.kind != RDKCTC)  /* is in a register? */
      return vd->vd.ridx + 1;
  }
  return 0;  /* no variables in registers */
}


/*
** Return the number of variables in the register stack for the given
** function.
*/
int luaY_nvarstack (FuncState *fs) {
  return reglevel(fs, fs->nactvar);
}


/*
** Get the debug-information entry for current variable 'vidx'.
*/
static LocVar *localdebuginfo (FuncState *fs, int vidx) {
  Vardesc *vd = getlocalvardesc(fs,  vidx);
  if (vd->vd.kind == RDKCTC)
    return NULL;  /* no debug info. for constants */
  else {
    int idx = vd->vd.pidx;
    lua_assert(idx < fs->ndebugvars);
    return &fs->f->locvars[idx];
  }
}


/*
** Create an expression representing variable 'vidx'
*/
static void init_var (FuncState *fs, expdesc *e, int vidx) {
  e->f = e->t = NO_JUMP;
  e->k = VLOCAL;
  e->u.var.vidx = vidx;
  e->u.var.ridx = getlocalvardesc(fs, vidx)->vd.ridx;
}


/*
** Raises an error if variable described by 'e' is read only
*/
static void check_readonly (LexState *ls, expdesc *e) {
  FuncState *fs = ls->fs;
  TString *varname = NULL;  /* to be set if variable is const */
  switch (e->k) {
    case VCONST: {
      varname = ls->dyd->actvar.arr[e->u.info].vd.name;
      break;
    }
    case VLOCAL: {
      Vardesc *vardesc = getlocalvardesc(fs, e->u.var.vidx);
      if (vardesc->vd.kind != VDKREG)  /* not a regular variable? */
        varname = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &fs->f->upvalues[e->u.info];
      if (up->kind != VDKREG)
        varname = up->name;
      break;
    }
    default:
      return;  /* other cases cannot be read-only */
  }
  if (varname) {
    const char *msg = luaO_pushfstring(ls->L,
       "[!] 错误: 无法给常量变量'%s'赋值", getstr(varname));
    luaK_semerror(ls, msg);  /* error */
  }
}


/*
** Start the scope for the last 'nvars' created variables.
*/
static void adjustlocalvars (LexState *ls, int nvars) {
  FuncState *fs = ls->fs;
  int reglevel = luaY_nvarstack(fs);
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = fs->nactvar++;
    Vardesc *var = getlocalvardesc(fs, vidx);
    var->vd.ridx = reglevel++;
    var->vd.pidx = registerlocalvar(ls, fs, var->vd.name);
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
static void removevars (FuncState *fs, int tolevel) {
  fs->ls->dyd->actvar.n -= (fs->nactvar - tolevel);
  while (fs->nactvar > tolevel) {
    LocVar *var = localdebuginfo(fs, --fs->nactvar);
    if (var)  /* does it have debug information? */
      var->endpc = fs->pc;
  }
}


/*
** Search the upvalues of the function 'fs' for one
** with the given 'name'.
*/
static int searchupvalue (FuncState *fs, TString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues;
  for (i = 0; i < fs->nups; i++) {
    if (eqstr(up[i].name, name)) return i;
  }
  return -1;  /* not found */
}


static Upvaldesc *allocupvalue (FuncState *fs) {
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  checklimit(fs, fs->nups + 1, MAXUPVAL, "upvalues");
  luaM_growvector(fs->ls->L, f->upvalues, fs->nups, f->sizeupvalues,
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  return &f->upvalues[fs->nups++];
}


static int newupvalue (FuncState *fs, TString *name, expdesc *v) {
  Upvaldesc *up = allocupvalue(fs);
  FuncState *prev = fs->prev;
  if (v->k == VLOCAL) {
    up->instack = 1;
    up->idx = v->u.var.ridx;
    up->kind = getlocalvardesc(prev, v->u.var.vidx)->vd.kind;
    lua_assert(eqstr(name, getlocalvardesc(prev, v->u.var.vidx)->vd.name));
  }
  else {
    up->instack = 0;
    up->idx = cast_byte(v->u.info);
    up->kind = prev->f->upvalues[v->u.info].kind;
    lua_assert(eqstr(name, prev->f->upvalues[v->u.info].name));
  }
  up->name = name;
  luaC_objbarrier(fs->ls->L, fs->f, name);
  return fs->nups - 1;
}


/*
** Look for an active local variable with the name 'n' in the
** function 'fs'. If found, initialize 'var' with it and return
** its expression kind; otherwise return -1.
*/
static int searchvar (FuncState *fs, TString *n, expdesc *var) {
  int i;
  for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
    Vardesc *vd = getlocalvardesc(fs, i);
    if (eqstr(n, vd->vd.name)) {  /* found? */
      if (vd->vd.kind == RDKCTC)  /* compile-time constant? */
        init_exp(var, VCONST, fs->firstlocal + i);
      else  /* real variable */
        init_var(fs, var, i);
      return var->k;
    }
  }
  return -1;  /* not found */
}


/*
** Mark block where variable at given level was defined
** (to emit close instructions later).
*/
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level)
    bl = bl->previous;
  bl->upval = 1;
  fs->needclose = 1;
}


/*
** Mark that current block has a to-be-closed variable.
*/
static void marktobeclosed (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  bl->upval = 1;
  bl->insidetbc = 1;
  fs->needclose = 1;
}


/*
** Find a variable with the given name 'n'. If it is an upvalue, add
** this upvalue into all intermediate functions. If it is a global, set
** 'var' as 'void' as a flag.
*/
static void singlevaraux (FuncState *fs, TString *n, expdesc *var, int base) {
  if (fs == NULL)  /* no more levels? */
    init_exp(var, VVOID, 0);  /* default is global */
  else {
    int v = searchvar(fs, n, var);  /* look up locals at current level */
    if (v >= 0) {  /* found? */
      if (v == VLOCAL && !base)
        markupval(fs, var->u.var.vidx);  /* local will be used as an upval */
    }
    else {  /* not found as local at current level; try upvalues */
      int idx = searchupvalue(fs, n);  /* try existing upvalues */
      if (idx < 0) {  /* not found? */
        singlevaraux(fs->prev, n, var, 0);  /* try upper levels */
        if (var->k == VLOCAL || var->k == VUPVAL)  /* local or upvalue? */
          idx  = newupvalue(fs, n, var);  /* will be a new upvalue */
        else  /* it is a global or a constant */
          return;  /* don't need to do anything at this level */
      }
      init_exp(var, VUPVAL, idx);  /* new or old upvalue */
    }
  }
}


/*
** Find a variable with the given name 'n', handling global variables
** too.
*/
static void singlevar (LexState *ls, expdesc *var) {
  TString *varname = str_checkname(ls);
  FuncState *fs = ls->fs;
  singlevaraux(fs, varname, var, 1);
  if (var->k == VVOID) {  /* global name? */
    expdesc key;
    singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
    lua_assert(var->k != VVOID);  /* this one must exist */
    codestring(&key, varname);  /* key is variable name */
    luaK_indexed(fs, var, &key);  /* env[varname] */
  }
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int needed = nvars - nexps;  /* extra values needed */
  luaK_checkstack(fs, needed);
  if (hasmultret(e->k)) {  /* last expression has multiple returns? */
    int extra = needed + 1;  /* discount last expression itself */
    if (extra < 0)
      extra = 0;
    luaK_setreturns(fs, e, extra);  /* last exp. provides the difference */
  }
  else {
    if (e->k != VVOID)  /* at least one expression? */
      luaK_exp2nextreg(fs, e);  /* close last expression */
    if (needed > 0)  /* missing values? */
      luaK_nil(fs, fs->freereg, needed);  /* complete with nils */
  }
  if (needed > 0)
    luaK_reserveregs(fs, needed);  /* registers for extra values */
  else  /* adding 'needed' is actually a subtraction */
    fs->freereg = cast_byte(fs->freereg + needed);  /* remove extra values */
}


#define enterlevel(ls)	luaE_incCstack(ls->L)


#define leavelevel(ls) ((ls)->L->nCcalls--)


/*
** Generates an error that a goto jumps into the scope of some
** local variable.
*/
static l_noret jumpscopeerror (LexState *ls, Labeldesc *gt) {
  const char *varname = getstr(getlocalvardesc(ls->fs, gt->nactvar)->vd.name);
  const char *msg = "<goto %s> at line %d jumps into the scope of local '%s'";
  msg = luaO_pushfstring(ls->L, msg, getstr(gt->name), gt->line, varname);
  luaK_semerror(ls, msg);  /* raise the error */
}


/*
** Solves the goto at index 'g' to given 'label' and removes it
** from the list of pending gotos.
** If it jumps into the scope of some variable, raises an error.
*/
static void solvegoto (LexState *ls, int g, Labeldesc *label) {
  int i;
  Labellist *gl = &ls->dyd->gt;  /* list of gotos */
  Labeldesc *gt = &gl->arr[g];  /* goto to be resolved */
  lua_assert(eqstr(gt->name, label->name));
  if (l_unlikely(gt->nactvar < label->nactvar))  /* enter some scope? */
    jumpscopeerror(ls, gt);
  luaK_patchlist(ls->fs, gt->pc, label->pc);
  for (i = g; i < gl->n - 1; i++)  /* remove goto from pending list */
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
}


/*
** Search for an active label with the given name.
*/
static Labeldesc *findlabel (LexState *ls, TString *name) {
  int i;
  Dyndata *dyd = ls->dyd;
  /* check labels in current function for a match */
  for (i = ls->fs->firstlabel; i < dyd->label.n; i++) {
    Labeldesc *lb = &dyd->label.arr[i];
    if (eqstr(lb->name, name))  /* correct label? */
      return lb;
  }
  return NULL;  /* label not found */
}


/*
** Adds a new label/goto in the corresponding list.
*/
static int newlabelentry (LexState *ls, Labellist *l, TString *name,
                          int line, int pc) {
  int n = l->n;
  luaM_growvector(ls->L, l->arr, n, l->size,
                  Labeldesc, SHRT_MAX, "labels/gotos");
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = ls->fs->nactvar;
  l->arr[n].close = 0;
  l->arr[n].pc = pc;
  l->n = n + 1;
  return n;
}


static int newgotoentry (LexState *ls, TString *name, int line, int pc) {
  return newlabelentry(ls, &ls->dyd->gt, name, line, pc);
}


/*
** Solves forward jumps. Check whether new label 'lb' matches any
** pending gotos in current block and solves them. Return true
** if any of the gotos need to close upvalues.
*/
static int solvegotos (LexState *ls, Labeldesc *lb) {
  Labellist *gl = &ls->dyd->gt;
  int i = ls->fs->bl->firstgoto;
  int needsclose = 0;
  while (i < gl->n) {
    if (eqstr(gl->arr[i].name, lb->name)) {
      needsclose |= gl->arr[i].close;
      solvegoto(ls, i, lb);  /* will remove 'i' from the list */
    }
    else
      i++;
  }
  return needsclose;
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
static int createlabel (LexState *ls, TString *name, int line,
                        int last) {
  FuncState *fs = ls->fs;
  Labellist *ll = &ls->dyd->label;
  int l = newlabelentry(ls, ll, name, line, luaK_getlabel(fs));
  if (last) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = fs->bl->nactvar;
  }
  if (solvegotos(ls, &ll->arr[l])) {  /* need close? */
    luaK_codeABC(fs, OP_CLOSE, luaY_nvarstack(fs), 0, 0);
    return 1;
  }
  return 0;
}


/*
** Adjust pending gotos to outer level of a block.
*/
static void movegotosout (FuncState *fs, BlockCnt *bl) {
  int i;
  Labellist *gl = &fs->ls->dyd->gt;
  /* correct pending gotos to current block */
  for (i = bl->firstgoto; i < gl->n; i++) {  /* for each pending goto */
    Labeldesc *gt = &gl->arr[i];
    /* leaving a variable scope? */
    if (reglevel(fs, gt->nactvar) > reglevel(fs, bl->nactvar))
      gt->close |= bl->upval;  /* jump may need a close */
    gt->nactvar = bl->nactvar;  /* update goto level */
  }
}


static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstlabel = fs->ls->dyd->label.n;
  bl->firstgoto = fs->ls->dyd->gt.n;
  bl->upval = 0;
  bl->insidetbc = (fs->bl != NULL && fs->bl->insidetbc);
  bl->previous = fs->bl;
  fs->bl = bl;
  bl->exports.arr = NULL;
  bl->exports.n = 0;
  bl->exports.size = 0;
  lua_assert(fs->freereg == luaY_nvarstack(fs));
}


/*
** generates an error for an undefined 'goto'.
*/
static l_noret undefgoto (LexState *ls, Labeldesc *gt) {
  const char *msg;
  if (eqstr(gt->name, luaS_newliteral(ls->L, "break"))) {
    msg = "在 %d 发现 break 语句位于循环外部 ";
    msg = luaO_pushfstring(ls->L, msg, gt->line);
  }
  else {
    msg = "未找到可见的标签 '%s' for <goto> at line %d";
    msg = luaO_pushfstring(ls->L, msg, getstr(gt->name), gt->line);
  }
  luaK_semerror(ls, msg);
}


static void add_export(LexState *ls, TString *name) {
  BlockCnt *bl = ls->fs->bl;
  if (bl->exports.n >= bl->exports.size) {
    bl->exports.size = (bl->exports.size == 0) ? 4 : bl->exports.size * 2;
    bl->exports.arr = luaM_reallocvector(ls->L, bl->exports.arr,
                                         bl->exports.n, bl->exports.size, TString*);
  }
  bl->exports.arr[bl->exports.n++] = name;
}

static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->bl;
  LexState *ls = fs->ls;
  if (bl->exports.n > 0) {
    int reg = fs->freereg;
    int pc = luaK_codeABC(fs, OP_NEWTABLE, reg, 0, 0);
    expdesc t;
    int i;
    luaK_code(fs, 0); /* Extra arg for NEWTABLE */
    init_exp(&t, VNONRELOC, reg);
    luaK_reserveregs(fs, 1);

    for (i = 0; i < bl->exports.n; i++) {
       expdesc k, v;
       TString *name = bl->exports.arr[i];
       expdesc t_copy = t;
       codestring(&k, name);
       singlevaraux(fs, name, &v, 1);
       luaK_exp2anyreg(fs, &v);
       luaK_indexed(fs, &t_copy, &k);
       luaK_storevar(fs, &t_copy, &v);
    }
    luaK_settablesize(fs, pc, reg, 0, bl->exports.n);
    luaK_ret(fs, reg, 1);

    luaM_freearray(ls->L, bl->exports.arr, bl->exports.size);
    bl->exports.n = 0;
    bl->exports.size = 0;
    bl->exports.arr = NULL;
  }
  int hasclose = 0;
  int stklevel = reglevel(fs, bl->nactvar);  /* level outside the block */
  if (bl->isloop)  /* fix pending breaks? */
    hasclose = createlabel(ls, luaS_newliteral(ls->L, "break"), 0, 0);
  if (!hasclose && bl->previous && bl->upval)
    luaK_codeABC(fs, OP_CLOSE, stklevel, 0, 0);
  fs->bl = bl->previous;
  removevars(fs, bl->nactvar);
  lua_assert(bl->nactvar == fs->nactvar);
  fs->freereg = stklevel;  /* free registers */
  ls->dyd->label.n = bl->firstlabel;  /* remove local labels */
  if (bl->previous)  /* inner block? */
    movegotosout(fs, bl);  /* update pending gotos to outer block */
  else {
    if (bl->firstgoto < ls->dyd->gt.n)  /* pending gotos in outer block? */
      undefgoto(ls, &ls->dyd->gt.arr[bl->firstgoto]);  /* error */
  }
}


/*
** adds a new prototype into list of prototypes
*/
static Proto *addprototype (LexState *ls) {
  Proto *clp;
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;  /* prototype of current function */
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep)
      f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = clp = luaF_newproto(L);
  luaC_objbarrier(L, f, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
static void codeclosure (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
  luaK_exp2nextreg(fs, v);  /* fix it at the last register */
}


static void open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  Proto *f = fs->f;
  fs->prev = ls->fs;  /* linked list of funcstates */
  fs->ls = ls;
  ls->fs = fs;
  fs->pc = 0;
  fs->previousline = f->linedefined;
  fs->iwthabs = 0;
  fs->lasttarget = 0;
  fs->freereg = 0;
  fs->nk = 0;
  fs->nabslineinfo = 0;
  fs->np = 0;
  fs->nups = 0;
  fs->ndebugvars = 0;
  fs->nactvar = 0;
  fs->needclose = 0;
  fs->firstlocal = ls->dyd->actvar.n;
  fs->firstlabel = ls->dyd->label.n;
  fs->bl = NULL;
  f->source = ls->source;
  luaC_objbarrier(ls->L, f, f->source);
  f->maxstacksize = 2;  /* registers 0/1 are always valid */
  enterblock(fs, bl, 0);
}


static void close_func (LexState *ls) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  leaveblock(fs);
  luaK_ret(fs, luaY_nvarstack(fs), 0);  /* final return */
  lua_assert(fs->bl == NULL);
  luaK_finish(fs);
  luaM_shrinkvector(L, f->code, f->sizecode, fs->pc, Instruction);
  luaM_shrinkvector(L, f->lineinfo, f->sizelineinfo, fs->pc, ls_byte);
  luaM_shrinkvector(L, f->abslineinfo, f->sizeabslineinfo,
                       fs->nabslineinfo, AbsLineInfo);
  luaM_shrinkvector(L, f->k, f->sizek, fs->nk, TValue);
  luaM_shrinkvector(L, f->p, f->sizep, fs->np, Proto *);
  luaM_shrinkvector(L, f->locvars, f->sizelocvars, fs->ndebugvars, LocVar);
  luaM_shrinkvector(L, f->upvalues, f->sizeupvalues, fs->nups, Upvaldesc);
  ls->fs = fs->prev;
  luaC_checkGC(L);
}


/*
** Create a global variable with the given name.
*/
static void buildglobal (LexState *ls, TString *varname, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc key;
  singlevaraux(fs, ls->envn, var, 1);  /* get environment variable */
  lua_assert(var->k != VVOID);  /* this one must exist */
  codestring(&key, varname);  /* key is variable name */
  luaK_indexed(fs, var, &key);  /* env[varname] */
}


/*
** Create a new variable with the given name and kind.
** Return its index in the function.
*/
static int new_varkind (LexState *ls, TString *name, lu_byte kind) {
  lua_State *L = ls->L;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  Vardesc *var;
  checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal,
                 MAXVARS, "local variables");
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1,
                  dyd->actvar.size, Vardesc, USHRT_MAX, "local variables");
  var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = kind;
  var->vd.name = name;
  return dyd->actvar.n - 1 - fs->firstlocal;
}

/*=================================================*/
/* GRAMMAR RULES */
/*=================================================*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->t.token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
    case TK_CASE: case TK_DEFAULT:
      return 1;
    case TK_DOLLAR: {
       int la = luaX_lookahead(ls);
       if (la == TK_NAME) {
          const char *name = getstr(ls->lookahead.seminfo.ts);
          if (strcmp(name, "else") == 0 ||
              strcmp(name, "elseif") == 0 ||
              strcmp(name, "end") == 0) {
             return 1;
          }
       }
       return 0;
    }
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


static void statlist (LexState *ls) {
  /* statlist -> { stat [';'] } */
  while (!block_follow(ls, 1)) {
    if (ls->t.token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }
    statement(ls);
  }
}


static void fieldsel (LexState *ls, expdesc *v) {
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *fs = ls->fs;
  expdesc key;
  luaK_exp2anyregup(fs, v);
  luaX_next(ls);  /* skip the dot or colon */
  
  /* Allow keywords as field names */
  if (ls->t.token == TK_NAME) {
    codename(ls, &key);
  }
  else {
    /* Handle keywords as field names */
    TString *ts;
    switch (ls->t.token) {
      /* Reserved words that can be used as field names */
      case TK_AND: ts = luaS_newliteral(ls->L, "and"); break;
      case TK_ASM: ts = luaS_newliteral(ls->L, "asm"); break;
      case TK_BREAK: ts = luaS_newliteral(ls->L, "break"); break;
      case TK_CASE: ts = luaS_newliteral(ls->L, "case"); break;
      case TK_CATCH: ts = luaS_newliteral(ls->L, "catch"); break;
      case TK_COMMAND: ts = luaS_newliteral(ls->L, "command"); break;
      case TK_CONST: ts = luaS_newliteral(ls->L, "const"); break;
      case TK_CONTINUE: ts = luaS_newliteral(ls->L, "continue"); break;
      case TK_DEFAULT: ts = luaS_newliteral(ls->L, "default"); break;
      case TK_DO: ts = luaS_newliteral(ls->L, "do"); break;
      case TK_ELSE: ts = luaS_newliteral(ls->L, "else"); break;
      case TK_ELSEIF: ts = luaS_newliteral(ls->L, "elseif"); break;
      case TK_END: ts = luaS_newliteral(ls->L, "end"); break;
      case TK_ENUM: ts = luaS_newliteral(ls->L, "enum"); break;
      case TK_FALSE: ts = luaS_newliteral(ls->L, "false"); break;
      case TK_FINALLY: ts = luaS_newliteral(ls->L, "finally"); break;
      case TK_FOR: ts = luaS_newliteral(ls->L, "for"); break;
      case TK_FUNCTION: ts = luaS_newliteral(ls->L, "function"); break;
      case TK_GLOBAL: ts = luaS_newliteral(ls->L, "global"); break;
      case TK_GOTO: ts = luaS_newliteral(ls->L, "goto"); break;
      case TK_IF: ts = luaS_newliteral(ls->L, "if"); break;
      case TK_IN: ts = luaS_newliteral(ls->L, "in"); break;
      case TK_IS: ts = luaS_newliteral(ls->L, "is"); break;
      case TK_LAMBDA: ts = luaS_newliteral(ls->L, "lambda"); break;
      case TK_LOCAL: ts = luaS_newliteral(ls->L, "local"); break;
      case TK_NIL: ts = luaS_newliteral(ls->L, "nil"); break;
      case TK_NOT: ts = luaS_newliteral(ls->L, "not"); break;
      case TK_OR: ts = luaS_newliteral(ls->L, "or"); break;
      case TK_REPEAT: ts = luaS_newliteral(ls->L, "repeat"); break;
      case TK_RETURN: ts = luaS_newliteral(ls->L, "return"); break;
      case TK_SWITCH: ts = luaS_newliteral(ls->L, "switch"); break;
      case TK_TAKE: ts = luaS_newliteral(ls->L, "take"); break;
      case TK_THEN: ts = luaS_newliteral(ls->L, "then"); break;
      case TK_TRUE: ts = luaS_newliteral(ls->L, "true"); break;
      case TK_TRY: ts = luaS_newliteral(ls->L, "try"); break;
      case TK_UNTIL: ts = luaS_newliteral(ls->L, "until"); break;
      case TK_WHEN: ts = luaS_newliteral(ls->L, "when"); break;
      case TK_WITH: ts = luaS_newliteral(ls->L, "with"); break;
      case TK_WHILE: ts = luaS_newliteral(ls->L, "while"); break;
      case TK_KEYWORD: ts = luaS_newliteral(ls->L, "keyword"); break;
      case TK_OPERATOR: ts = luaS_newliteral(ls->L, "operator"); break;
      default: error_expected(ls, TK_NAME);
    }
    codestring(&key, ts);
    luaX_next(ls);
  }
  luaK_indexed(fs, v, &key);
}


static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  luaX_next(ls);  /* skip the '[' */
  expr(ls, v);
  luaK_exp2val(ls->fs, v);
  checknext(ls, ']');
}


/*
** 检查当前是否是切片语法的开始
** 切片语法: [start:end] 或 [start:end:step] 或 [:end] 或 [start:] 等
** 返回 1 表示是切片语法，0 表示是普通索引
** 
** @param ls 词法分析器状态
** @return 1 表示是切片语法，0 表示是普通索引
*/
static int is_slice_syntax (LexState *ls) {
  /* 已经在 '[' 后面，检查第一个 token 是否是 ':' */
  if (ls->t.token == ':') {
    return 1;  /* [:end] 或 [::step] 等形式 */
  }
  /* 需要向前看来确定是否是切片 */
  /* 解析第一个表达式，然后检查是否后面跟着 ':' */
  /* 但这样会消耗 token，所以我们使用 lookahead */
  return 0;  /* 默认按普通索引处理，在 yindex_or_slice 中进一步判断 */
}


/*
** 处理切片表达式语法: t[start:end] 或 t[start:end:step]
** 支持的形式:
** - t[a:b]     从索引 a 到 b（包含两端）
** - t[a:b:s]   从索引 a 到 b，步长为 s
** - t[:b]      等价于 t[1:b]
** - t[a:]      等价于 t[a:#t]
** - t[:]       等价于 t[1:#t] (复制整个数组部分)
** - t[::s]     步长为 s 的整个数组
** - 负索引：t[-3:-1] 取倒数3到倒数1个元素
** 
** 生成代码：
** - 将源表放入寄存器 base
** - 将 start, end, step 放入 base+1, base+2, base+3
** - 生成 OP_SLICE 指令
** 
** @param ls 词法分析器状态
** @param v 输入为源表表达式，输出为切片结果表达式
*/
static void sliceexpr (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int base;  /* 基础寄存器 */
  expdesc start_exp, end_exp, step_exp;
  int has_step = 0;
  
  /* 已经跳过了 '[' */
  
  /* 将源表放入寄存器 */
  luaK_exp2nextreg(fs, v);
  base = v->u.info;  /* 源表在 base 寄存器 */
  
  /* 解析 start 表达式 */
  if (ls->t.token == ':') {
    /* 省略 start，使用 nil 表示 1 */
    init_exp(&start_exp, VNIL, 0);
  }
  else {
    expr(ls, &start_exp);
  }
  luaK_exp2nextreg(fs, &start_exp);  /* start 在 base+1 */
  
  /* 必须有第一个 ':' */
  checknext(ls, ':');
  
  /* 解析 end 表达式 */
  if (ls->t.token == ']' || ls->t.token == ':') {
    /* 省略 end，使用 nil 表示 #t */
    init_exp(&end_exp, VNIL, 0);
  }
  else {
    expr(ls, &end_exp);
  }
  luaK_exp2nextreg(fs, &end_exp);  /* end 在 base+2 */
  
  /* 检查是否有 step */
  if (testnext(ls, ':')) {
    has_step = 1;
    if (ls->t.token == ']') {
      /* 省略 step，使用 nil 表示 1 */
      init_exp(&step_exp, VNIL, 0);
    }
    else {
      expr(ls, &step_exp);
    }
    luaK_exp2nextreg(fs, &step_exp);  /* step 在 base+3 */
  }
  else {
    /* 没有 step，使用 nil 表示 1 */
    init_exp(&step_exp, VNIL, 0);
    luaK_exp2nextreg(fs, &step_exp);  /* step 在 base+3 */
  }
  
  checknext(ls, ']');  /* 必须以 ']' 结束 */
  
  /* 生成 OP_SLICE 指令 */
  /* A = 结果寄存器（复用 base）, B = 源表寄存器, C = 标志 */
  luaK_codeABC(fs, OP_SLICE, base, base, has_step);
  
  /* 释放临时寄存器 (start, end, step) */
  fs->freereg = base + 1;
  
  /* 设置结果表达式 */
  v->k = VNONRELOC;
  v->u.info = base;
}


/*
** 处理索引或切片语法: t[exp] 或 t[start:end:step]
** 首先解析第一个表达式或检测 ':'，然后决定是普通索引还是切片
** 
** @param ls 词法分析器状态
** @param v 输入为源表表达式，输出为索引/切片结果表达式
** @return 1 如果是切片，0 如果是普通索引
*/
static int yindex_or_slice (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  
  luaX_next(ls);  /* skip the '[' */
  
  /* 检查是否是切片语法: 第一个 token 是 ':' */
  if (ls->t.token == ':') {
    /* 这是切片语法: [:end] 或 [::step] 等形式 */
    
    /* 将源表放入寄存器 */
    luaK_exp2nextreg(fs, v);
    int base = v->u.info;
    
    expdesc start_exp, end_exp, step_exp;
    int has_step = 0;
    
    /* start 省略，使用 nil */
    init_exp(&start_exp, VNIL, 0);
    luaK_exp2nextreg(fs, &start_exp);
    
    /* 跳过第一个 ':' */
    luaX_next(ls);
    
    /* 解析 end */
    if (ls->t.token == ']' || ls->t.token == ':') {
      init_exp(&end_exp, VNIL, 0);
    }
    else {
      expr(ls, &end_exp);
    }
    luaK_exp2nextreg(fs, &end_exp);
    
    /* 检查是否有 step */
    if (testnext(ls, ':')) {
      has_step = 1;
      if (ls->t.token == ']') {
        init_exp(&step_exp, VNIL, 0);
      }
      else {
        expr(ls, &step_exp);
      }
      luaK_exp2nextreg(fs, &step_exp);
    }
    else {
      init_exp(&step_exp, VNIL, 0);
      luaK_exp2nextreg(fs, &step_exp);
    }
    
    checknext(ls, ']');
    
    luaK_codeABC(fs, OP_SLICE, base, base, has_step);
    fs->freereg = base + 1;
    
    v->k = VNONRELOC;
    v->u.info = base;
    return 1;  /* 是切片 */
  }
  
  /* 
  ** 关键：在解析 key 表达式之前，先固定源表的位置
  ** 这样在 expr(ls, &key) 执行期间，v 的值不会被破坏
  */
  luaK_exp2anyregup(fs, v);
  
  /* 解析第一个表达式 */
  expdesc key;
  expr(ls, &key);
  
  /* 检查表达式后面是否跟着 ':' */
  if (ls->t.token == ':') {
    /* 这是切片语法: [start:end] 或 [start:end:step] */
    
    /* 将源表移动到下一个连续寄存器位置（切片需要连续的寄存器布局） */
    luaK_exp2nextreg(fs, v);
    int base = v->u.info;
    
    /* 将 start 表达式放入下一个寄存器 */
    luaK_exp2nextreg(fs, &key);
    
    expdesc end_exp, step_exp;
    int has_step = 0;
    
    /* 跳过 ':' */
    luaX_next(ls);
    
    /* 解析 end */
    if (ls->t.token == ']' || ls->t.token == ':') {
      init_exp(&end_exp, VNIL, 0);
    }
    else {
      expr(ls, &end_exp);
    }
    luaK_exp2nextreg(fs, &end_exp);
    
    /* 检查是否有 step */
    if (testnext(ls, ':')) {
      has_step = 1;
      if (ls->t.token == ']') {
        init_exp(&step_exp, VNIL, 0);
      }
      else {
        expr(ls, &step_exp);
      }
      luaK_exp2nextreg(fs, &step_exp);
    }
    else {
      init_exp(&step_exp, VNIL, 0);
      luaK_exp2nextreg(fs, &step_exp);
    }
    
    checknext(ls, ']');
    
    luaK_codeABC(fs, OP_SLICE, base, base, has_step);
    fs->freereg = base + 1;
    
    v->k = VNONRELOC;
    v->u.info = base;
    return 1;  /* 是切片 */
  }
  
  /* 普通索引: [exp] */
  /* v 已经在前面通过 luaK_exp2anyregup 固定好了 */
  luaK_exp2val(fs, &key);
  checknext(ls, ']');
  luaK_indexed(fs, v, &key);
  return 0;  /* 不是切片 */
}


/*
** {===========================================================
** Rules for Constructors
** ============================================================
*/


typedef struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of 'record' elements */
  int na;  /* number of array elements already stored */
  int tostore;  /* number of array elements pending to be stored */
} ConsControl;


static void recfield (LexState *ls, ConsControl *cc) {
  /* recfield -> (NAME | '['exp']') = exp */
  FuncState *fs = ls->fs;
  int reg = ls->fs->freereg;
  expdesc tab, key, val;
  if (ls->t.token == TK_NAME) {
    checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
    codename(ls, &key);
  }
  else  /* ls->t.token == '[' */
    yindex(ls, &key);
  cc->nh++;
  if (ls->t.token != '=' && ls->t.token != ':')
    error_expected(ls, '=');
  luaX_next(ls);
  tab = *cc->t;
  luaK_indexed(fs, &tab, &key);
  expr(ls, &val);
  luaK_storevar(fs, &tab, &val);
  fs->freereg = reg;  /* free registers */
}


static void closelistfield (FuncState *fs, ConsControl *cc) {
  if (cc->v.k == VVOID) return;  /* there is no list item */
  luaK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);  /* flush */
    cc->na += cc->tostore;
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.k)) {
    luaK_setmultret(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.k != VVOID)
      luaK_exp2nextreg(fs, &cc->v);
    luaK_setlist(fs, cc->t->u.info, cc->na, cc->tostore);
  }
  cc->na += cc->tostore;
}


static void listfield (LexState *ls, ConsControl *cc) {
  /* listfield -> exp */
  expr(ls, &cc->v);
  cc->tostore++;
}


static void field (LexState *ls, ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->t.token) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      int lookahead = luaX_lookahead(ls);
      if (lookahead != '=' && lookahead != ':')  /* expression? */
        listfield(ls, cc);
      else
        recfield(ls, cc);
      break;
    }
    case '[': {
      recfield(ls, cc);
      break;
    }
    default: {
      listfield(ls, cc);
      break;
    }
  }
}


static void constructor (LexState *ls, expdesc *t) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  ConsControl cc;
  luaK_code(fs, 0);  /* space for extra arg. */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VNONRELOC, fs->freereg);  /* table will be at stack top */
  luaK_reserveregs(fs, 1);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
  checknext(ls, '{');
  do {
    lua_assert(cc.v.k == VVOID || cc.tostore > 0);
    if (ls->t.token == '}') break;
    closelistfield(fs, &cc);
    field(ls, &cc);
  } while (testnext(ls, ',') || testnext(ls, ';'));
  check_match(ls, '}', '{', line);
  lastlistfield(fs, &cc);
  luaK_settablesize(fs, pc, t->u.info, cc.na, cc.nh);
}

//static void constructor2 (LexState *ls, expdesc *t) {
//  /* constructor -> '{' [ field { sep field } [sep] ] '}'
//     sep -> ',' | ';' */
//  FuncState *fs = ls->fs;
//  int line = ls->linenumber;
//  int pc = luaK_codeABC(fs, OP_NEWARRAY, 0, 0, 0);
//  ConsControl cc;
//  luaK_code(fs, 0);  /* space for extra arg. */
//  cc.na = cc.nh = cc.tostore = 0;
//  cc.t = t;
//  init_exp(t, VNONRELOC, fs->freereg);  /* table will be at stack top */
//  luaK_reserveregs(fs, 1);
//  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
//  checknext(ls, '[');
//  do {
//    lua_assert(cc.v.k == VVOID || cc.tostore > 0);
//    if (ls->t.token == ']') break;
//    closelistfield(fs, &cc);
//    listfield(ls, &cc);
//  } while (testnext(ls, ',') || testnext(ls, ';'));
//  check_match(ls, ']', '[', line);
//  lastlistfield(fs, &cc);
//  luaK_setarraysize(fs, pc, t->u.info, cc.na, cc.nh);
//}

/* }=========================================================== */


static void setvararg (FuncState *fs, int nparams) {
  fs->f->is_vararg = 1;
  luaK_codeABC(fs, OP_VARARGPREP, nparams, 0, 0);
}


static void parlist (LexState *ls) {
  /* parlist -> [ {NAME ','} (NAME | '...') ] */
  FuncState *fs = ls->fs;
  Proto *f = fs->f;
  int nparams = 0;
  int isvararg = 0;
  if (ls->t.token != ')') {  /* is 'parlist' not empty? */
    do {
      switch (ls->t.token) {
        case TK_NAME: {
          new_localvar(ls, str_checkname(ls));
          nparams++;
          break;
        }
        case TK_DOTS: {
          luaX_next(ls);
          isvararg = 1;
          break;
        }
        default: luaX_syntaxerror(ls, "<name> or '...' expected");
      }
    } while (!isvararg && testnext(ls, ','));
  }
  adjustlocalvars(ls, nparams);
  f->numparams = cast_byte(fs->nactvar);
  if (isvararg)
    setvararg(fs, f->numparams);  /* declared vararg */
  luaK_reserveregs(fs, fs->nactvar);  /* reserve registers for parameters */
}


/**
 * 解析函数体
 * 支持两种语法：
 *   1. 标准语法: function name(params) block end
 *   2. 大括号语法糖: function name{block} (无参数函数的简写形式)
 * 
 * @param ls 词法状态
 * @param e 表达式描述符，用于存储闭包结果
 * @param ismethod 是否为方法（需要添加self参数）
 * @param line 函数定义所在行号
 */
static void body (LexState *ls, expdesc *e, int ismethod, int line) {
  /* body ->  '(' parlist ')' block END | '{' block '}' */
  FuncState new_fs;
  BlockCnt bl;
  int use_brace = 0;  /* 标记是否使用大括号语法 */
  new_fs.f = addprototype(ls);
  new_fs.f->linedefined = line;
  open_func(ls, &new_fs, &bl);
  
  /* 检测是否使用大括号语法糖: function name{} */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* 跳过 '{' */
    if (ismethod) {
      new_localvarliteral(ls, "self");  /* 为方法创建 'self' 参数 */
      adjustlocalvars(ls, 1);
    }
    /* 大括号语法不支持参数列表，视为无参函数 */
    
    /* 对于大括号语法，手动解析语句列表直到遇到 '}' */
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      if (ls->t.token == TK_RETURN) {
        statement(ls);
        break;  /* 'return' 必须是最后一条语句 */
      }
      statement(ls);
    }
  }
  else {
    /* 标准语法: (parlist) block end */
    checknext(ls, '(');
    if (ismethod) {
      new_localvarliteral(ls, "self");  /* 为方法创建 'self' 参数 */
      adjustlocalvars(ls, 1);
    }
    parlist(ls);
    checknext(ls, ')');
    statlist(ls);  /* 标准语法使用 statlist */
  }
  
  new_fs.f->lastlinedefined = ls->linenumber;
  
  if (use_brace) {
    check_match(ls, '}', '{', line);  /* 大括号语法用 '}' 结束 */
  }
  else {
    check_match(ls, TK_END, TK_FUNCTION, line);  /* 标准语法用 'end' 结束 */
  }
  
  codeclosure(ls, e);
  close_func(ls);
}


static void lambda_parlist(LexState *ls) {
    /* lambda_parlist -> '(' [ param { ',' param } ] ')' */
    /* lambda_parlist -> [ param { ',' param } ] */
    if (testnext(ls, '(')) {
        parlist(ls);
        checknext(ls, ')');
        return;
    }
    FuncState *fs = ls->fs;
    Proto *f = fs->f;
    int nparams = 0;
    f->is_vararg = 0;
    if (ls->t.token == TK_NAME || ls->t.token == TK_DOTS) {
        do {
            switch (ls->t.token) {
                case TK_NAME: {  /* param -> NAME */
                    new_localvar(ls, str_checkname(ls));
                    nparams++;
                    break;
                }
                case TK_DOTS: {  /* param -> '...' */
                    luaX_next(ls);
                    f->is_vararg = 1;
                    break;
                }
                default: luaX_syntaxerror(ls, "<name> or '...' expected");
            }
        } while (!f->is_vararg && testnext(ls, ','));
    }
    adjustlocalvars(ls, nparams);
    f->numparams = cast_byte(fs->nactvar);
    luaK_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
}


static void lambda_body(LexState *ls, expdesc *e, int line) {
    /* lambda_body -> lambda_parlist -> explist */
    /* lambda_body -> lambda_parlist [ '=>' ] stat */
    FuncState new_fs;
    BlockCnt bl;
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &bl);
    lambda_parlist(ls);
    if (testnext(ls, TK_LET)||testnext(ls, ':')) {
        enterlevel(ls);
        retstat(ls);
        lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
                   ls->fs->freereg >= ls->fs->nactvar);
        ls->fs->freereg = ls->fs->nactvar;  /* free registers */
        leavelevel(ls);
    } else {
        testnext(ls, TK_MEAN);
        statement(ls);
    }
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, e);
    close_func(ls);
}


static int explist (LexState *ls, expdesc *v) {
  /* explist -> expr { ',' expr } */
  int n = 1;  /* at least one expression */
  expr(ls, v);
  while (testnext(ls, ',')) {
    luaK_exp2nextreg(ls->fs, v);
    expr(ls, v);
    n++;
  }
  return n;
}


static void funcargs (LexState *ls, expdesc *f, int line) {
  FuncState *fs = ls->fs;
  expdesc args;
  int base, nparams;
  switch (ls->t.token) {
    case '(': {  /* funcargs -> '(' [ explist ] ')' */
      luaX_next(ls);
      if (ls->t.token == ')')  /* arg list is empty? */
        args.k = VVOID;
      else {
        explist(ls, &args);
        if (hasmultret(args.k))
          luaK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      break;
    }
    case '{': {  /* funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING:
    case TK_RAWSTRING: {  /* funcargs -> STRING / RAWSTRING */
      codestring(&args, ls->t.seminfo.ts);
      luaX_next(ls);  /* must use 'seminfo' before 'next' */
      break;
    }
    default: {
      luaX_syntaxerror(ls, "function arguments expected");
    }
  }
  lua_assert(f->k == VNONRELOC);
  base = f->u.info;  /* base register for call */
  if (hasmultret(args.k))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.k != VVOID)
      luaK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  luaK_fixline(fs, line);
  fs->freereg = base+1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
}




/*
** {===========================================================
** Expression parsing
** ============================================================
*/


static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' | STRING | constructor | NEW | SUPER */
  switch (ls->t.token) {
    case '(': {
      int line = ls->linenumber;
      luaX_next(ls);
      if (ls->t.token == TK_NAME) {
        if (luaX_lookahead(ls) == TK_WALRUS) {
          TString *varname = ls->t.seminfo.ts;
          int save = ls->linenumber;
          luaX_next(ls);
          luaX_next(ls);
          expdesc e;
          expr(ls, &e);
          check_match(ls, ')', '(', save);
          luaK_dischargevars(ls->fs, &e);
          singlevaraux(ls->fs, varname, v, 1);
          if (v->k == VVOID) {
            expdesc key;
            singlevaraux(ls->fs, ls->envn, v, 1);
            codestring(&key, varname);
            luaK_indexed(ls->fs, v, &key);
          }
          luaK_storevar(ls->fs, v, &e);
          luaK_exp2nextreg(ls->fs, &e);
          init_exp(v, VNONRELOC, e.u.info);
          return;
        }
      }
      expr(ls, v);
      check_match(ls, ')', '(', line);
      luaK_dischargevars(ls->fs, v);
      return;
    }
    case TK_NAME: {
      /* 使用软关键字系统检查 new */
      if (softkw_test(ls, SKW_NEW, SOFTKW_CTX_EXPR)) {
        /* onew ClassName(args...) - 创建类实例 */
        newexpr(ls, v);
        return;
      }
      /* 使用软关键字系统检查 osuper（需要前瞻 . 或 :） */
      if (softkw_test(ls, SKW_SUPER, SOFTKW_CTX_EXPR)) {
        /* osuper.method 或 osuper:method - 调用父类方法 */
        superexpr(ls, v);
        return;
      }
      /* 普通标识符 */
      singlevar(ls, v);
      return;
    }
    case TK_STRING:
    case TK_RAWSTRING: {
      codestring(v, ls->t.seminfo.ts);
      luaX_next(ls);
      return;
    }
    case '{': {
      constructor(ls, v);
      return;
    }
    case TK_DOLLAR: {
      /* $func(...) compile-time call OR $macro */
      /* For now, we assume if it's in primaryexp, it's a value. */
      /* Pluto allows $lib.func() which evaluates to a constant. */
      /* Check if it is a supported compile-time function */

      /* If not supported, fallback to _KEYWORDS logic for compatibility */

      FuncState *fs = ls->fs;
      int line = ls->linenumber;
      TString *kwname;
      expdesc keywords_table, key_exp;
      
      luaX_next(ls);  /* Skip '$' */
      check(ls, TK_NAME);
      kwname = ls->t.seminfo.ts;
      
      /* Check for compile-time function call support here? */
      /* For simplicity in this step, we keep the _KEYWORDS fallback but TODO: add const expr support */
      /* We can implement a check here: if kwname matches a standard lib, try to execute */

      luaX_next(ls);  /* Skip name */

      /* Fallback to _KEYWORDS */
      singlevaraux(fs, luaS_newliteral(ls->L, "_KEYWORDS"), &keywords_table, 1);
      if (keywords_table.k == VVOID) {
        expdesc env_key;
        singlevaraux(fs, ls->envn, &keywords_table, 1);
        codestring(&env_key, luaS_newliteral(ls->L, "_KEYWORDS"));
        luaK_indexed(fs, &keywords_table, &env_key);
      }
      
      luaK_exp2anyreg(fs, &keywords_table);
      codestring(&key_exp, kwname);
      luaK_indexed(fs, &keywords_table, &key_exp);
      
      *v = keywords_table;
      return;
    }
    case TK_DOLLDOLL: {
      /**
       * 运算符调用语法: $$<运算符>(args)
       * 等价于: _OPERATORS["<运算符>"](args)
       * 
       * 用于调用 operator 关键字定义的自定义运算符
       * 示例: $$++(a) 调用 _OPERATORS["++"](a)
       *       $$^(a, b) 调用 _OPERATORS["^"](a, b)
       */
      FuncState *fs = ls->fs;
      TString *opname = NULL;
      const char *opstr = NULL;
      expdesc operators_table, key_exp;
      
      luaX_next(ls);  /* 跳过 '$$' */
      
      /* 解析运算符符号 */
      int tok = ls->t.token;
      switch (tok) {
        case TK_PLUSPLUS: opstr = "++"; break;
        case TK_CONCAT: opstr = ".."; break;
        case TK_IDIV: opstr = "//"; break;
        case TK_SHL: opstr = "<<"; break;
        case TK_SHR: opstr = ">>"; break;
        case TK_EQ: opstr = "=="; break;
        case TK_NE: opstr = "~="; break;
        case TK_LE: opstr = "<="; break;
        case TK_GE: opstr = ">="; break;
        case TK_PIPE: opstr = "|>"; break;
        case TK_REVPIPE: opstr = "<|"; break;
        case TK_SPACESHIP: opstr = "<=>"; break;
        case TK_NULLCOAL: opstr = "??"; break;
        case TK_ARROW: opstr = "->"; break;
        case TK_MEAN: opstr = "=>"; break;
        case TK_ADDEQ: opstr = "+="; break;
        case TK_SUBEQ: opstr = "-="; break;
        case TK_MULEQ: opstr = "*="; break;
        case TK_DIVEQ: opstr = "/="; break;
        case TK_MODEQ: opstr = "%="; break;
        case '+': opstr = "+"; break;
        case '-': opstr = "-"; break;
        case '*': opstr = "*"; break;
        case '/': opstr = "/"; break;
        case '%': opstr = "%"; break;
        case '^': opstr = "^"; break;
        case '#': opstr = "#"; break;
        case '&': opstr = "&"; break;
        case '|': opstr = "|"; break;
        case '~': opstr = "~"; break;
        case '<': opstr = "<"; break;
        case '>': opstr = ">"; break;
        case '@': opstr = "@"; break;
        case TK_NAME:
          opname = ls->t.seminfo.ts;
          break;
        case TK_STRING:
          opname = ls->t.seminfo.ts;
          break;
        default:
          luaX_syntaxerror(ls, "expected operator symbol after '$$'");
      }
      
      if (opstr != NULL) {
        opname = luaS_new(ls->L, opstr);
      }
      
      luaX_next(ls);  /* 跳过运算符符号 */
      
      /* 获取 _OPERATORS 表 */
      singlevaraux(fs, luaS_newliteral(ls->L, "_OPERATORS"), &operators_table, 1);
      if (operators_table.k == VVOID) {
        /* 从 _ENV 获取 _OPERATORS */
        expdesc env_key;
        singlevaraux(fs, ls->envn, &operators_table, 1);
        codestring(&env_key, luaS_newliteral(ls->L, "_OPERATORS"));
        luaK_indexed(fs, &operators_table, &env_key);
      }
      
      /* 获取 _OPERATORS[运算符] */
      luaK_exp2anyreg(fs, &operators_table);
      codestring(&key_exp, opname);
      luaK_indexed(fs, &operators_table, &key_exp);
      
      /* 返回函数表达式，让 suffixedexp 继续处理后续的函数调用 */
      *v = operators_table;
      return;
    }
    default: {
      luaX_syntaxerror(ls, "unexpected symbol");
    }
  }
}


/*
** 解析管道右侧的函数表达式
** 只解析主表达式和字段访问（. []），不递归处理管道运算符
** 确保管道运算符是左关联的
** 
** @param ls 词法状态
** @param v 输出表达式描述符
*/
static void pipe_funcexp (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  /* 先解析主表达式 */
  primaryexp(ls, v);
  /* 只处理字段访问，不处理管道 */
  for (;;) {
    switch (ls->t.token) {
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 '[' start:end:step ']' */
        yindex_or_slice(ls, v);
        break;
      }
      default: return;  /* 遇到其他 token 就停止 */
    }
  }
}


static void suffixedexp (LexState *ls, expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '?.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs | '|>' suffixedexp } */
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  primaryexp(ls, v);
  for (;;) {
    switch (ls->t.token) {
      case TK_OPTCHAIN: {  /* '?.' 可选链字段访问 */
        /*
        ** 可选链运算符: a?.b
        ** 功能描述：如果 a 为 nil，则结果为 nil；否则结果为 a.b
        ** 实现方式：使用 OP_TESTNIL 指令进行 nil 检测和短路跳转
        ** 生成代码序列：
        **   TESTNIL R R 0 k=1 ; 如果 R 不是 nil，跳过下一条 JMP
        **   JMP end           ; 是 nil，跳过字段访问
        **   GETFIELD R R "b"  ; 不是 nil，获取字段
        **   end:
        */
        expdesc key;
        int reg;
        int jmp_skip;
        int idx;
        
        /* 将表达式转换为寄存器 */
        luaK_dischargevars(fs, v);
        reg = luaK_exp2anyreg(fs, v);
        
        /* 生成 TESTNIL 指令：k=1 表示非nil时跳过下一条JMP */
        luaK_codeABCk(fs, OP_TESTNIL, reg, reg, 0, 1);
        /* 生成跳转指令：是 nil 时执行此JMP跳过后续字段访问 */
        jmp_skip = luaK_jump(fs);
        
        /* 不是 nil，进行正常字段访问 */
        luaX_next(ls);  /* 跳过 '?.' */
        
        /* 允许关键字作为字段名 */
        if (ls->t.token == TK_NAME) {
          codename(ls, &key);
        }
        else {
          /* 处理关键字作为字段名的情况 */
          TString *ts;
          switch (ls->t.token) {
            case TK_AND: ts = luaS_newliteral(ls->L, "and"); break;
            case TK_BREAK: ts = luaS_newliteral(ls->L, "break"); break;
            case TK_CASE: ts = luaS_newliteral(ls->L, "case"); break;
            case TK_CATCH: ts = luaS_newliteral(ls->L, "catch"); break;
            case TK_COMMAND: ts = luaS_newliteral(ls->L, "command"); break;
            case TK_CONST: ts = luaS_newliteral(ls->L, "const"); break;
            case TK_CONTINUE: ts = luaS_newliteral(ls->L, "continue"); break;
            case TK_DEFAULT: ts = luaS_newliteral(ls->L, "default"); break;
            case TK_DO: ts = luaS_newliteral(ls->L, "do"); break;
            case TK_ELSE: ts = luaS_newliteral(ls->L, "else"); break;
            case TK_ELSEIF: ts = luaS_newliteral(ls->L, "elseif"); break;
            case TK_END: ts = luaS_newliteral(ls->L, "end"); break;
            case TK_ENUM: ts = luaS_newliteral(ls->L, "enum"); break;
            case TK_FALSE: ts = luaS_newliteral(ls->L, "false"); break;
            case TK_FINALLY: ts = luaS_newliteral(ls->L, "finally"); break;
            case TK_FOR: ts = luaS_newliteral(ls->L, "for"); break;
            case TK_FUNCTION: ts = luaS_newliteral(ls->L, "function"); break;
            case TK_GLOBAL: ts = luaS_newliteral(ls->L, "global"); break;
            case TK_GOTO: ts = luaS_newliteral(ls->L, "goto"); break;
            case TK_IF: ts = luaS_newliteral(ls->L, "if"); break;
            case TK_IN: ts = luaS_newliteral(ls->L, "in"); break;
            case TK_IS: ts = luaS_newliteral(ls->L, "is"); break;
            case TK_LAMBDA: ts = luaS_newliteral(ls->L, "lambda"); break;
            case TK_LOCAL: ts = luaS_newliteral(ls->L, "local"); break;
            case TK_NIL: ts = luaS_newliteral(ls->L, "nil"); break;
            case TK_NOT: ts = luaS_newliteral(ls->L, "not"); break;
            case TK_OR: ts = luaS_newliteral(ls->L, "or"); break;
            case TK_REPEAT: ts = luaS_newliteral(ls->L, "repeat"); break;
            case TK_RETURN: ts = luaS_newliteral(ls->L, "return"); break;
            case TK_SWITCH: ts = luaS_newliteral(ls->L, "switch"); break;
            case TK_TAKE: ts = luaS_newliteral(ls->L, "take"); break;
            case TK_THEN: ts = luaS_newliteral(ls->L, "then"); break;
            case TK_TRUE: ts = luaS_newliteral(ls->L, "true"); break;
            case TK_TRY: ts = luaS_newliteral(ls->L, "try"); break;
            case TK_UNTIL: ts = luaS_newliteral(ls->L, "until"); break;
            case TK_WHEN: ts = luaS_newliteral(ls->L, "when"); break;
            case TK_WITH: ts = luaS_newliteral(ls->L, "with"); break;
            case TK_WHILE: ts = luaS_newliteral(ls->L, "while"); break;
            case TK_KEYWORD: ts = luaS_newliteral(ls->L, "keyword"); break;
            case TK_OPERATOR: ts = luaS_newliteral(ls->L, "operator"); break;
            default: error_expected(ls, TK_NAME);
          }
          codestring(&key, ts);
          luaX_next(ls);
        }
        
        /* 使用 luaK_indexed 获取字段名常量索引，但不释放寄存器 */
        /* 临时设置 v 的状态用于 luaK_indexed */
        v->k = VNONRELOC;
        v->u.info = reg;
        luaK_indexed(fs, v, &key);  /* v 变成 VINDEXSTR，v->u.ind.idx 是常量索引 */
        idx = v->u.ind.idx;
        
        /* 手动生成 GETFIELD 指令，结果存入 reg（覆盖原表的位置） */
        luaK_codeABC(fs, OP_GETFIELD, reg, reg, idx);
        
        /* 修复跳转目标：nil 时跳转到这里 */
        luaK_patchtohere(fs, jmp_skip);
        
        /* 重置表达式状态为 VNONRELOC，值在 reg 中，清除跳转列表 */
        v->k = VNONRELOC;
        v->u.info = reg;
        v->t = NO_JUMP;
        v->f = NO_JUMP;
        break;
      }
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 '[' start:end:step ']' */
        yindex_or_slice(ls, v);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        luaX_next(ls);
        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v, line);
        break;
      }
      case '(': case TK_STRING: case TK_RAWSTRING: case '{': {  /* funcargs */
        luaK_exp2nextreg(fs, v);
        funcargs(ls, v, line);
        break;
      }
      case TK_PIPE: {  /* '|>' */
        luaX_next(ls);
        expdesc e;
        /* 支持管道符右侧直接使用字面量和匿名函数 */
        switch (ls->t.token) {
          case TK_FUNCTION: {  /* 匿名函数 */
            body(ls, &e, 0, ls->linenumber);
            break;
          }
          case TK_LAMBDA: {  /* lambda表达式 */
            lambda_body(ls, &e, ls->linenumber);
            break;
          }
          case TK_INT: {  /* 整数常量 */
            init_exp(&e, VKINT, 0);
            e.u.ival = ls->t.seminfo.i;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {  /* 浮点数常量 */
            init_exp(&e, VKFLT, 0);
            e.u.nval = ls->t.seminfo.r;
            luaX_next(ls);
            break;
          }
          case TK_STRING:
          case TK_RAWSTRING: {  /* 字符串常量 */
            codestring(&e, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {  /* true常量 */
            init_exp(&e, VTRUE, 0);
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {  /* false常量 */
            init_exp(&e, VFALSE, 0);
            luaX_next(ls);
            break;
          }
          case TK_NIL: {  /* nil常量 */
            init_exp(&e, VNIL, 0);
            luaX_next(ls);
            break;
          }
          case '{': {  /* 表常量作为函数（返回自身的函数） */
            constructor(ls, &e);
            break;
          }
          default: {
            /* 解析函数表达式（不递归处理管道，确保左关联） */
            pipe_funcexp(ls, &e);
            break;
          }
        }
        /* 生成管道运算符代码 */
        luaK_pipe(fs, v, &e);
        break;
      }
      case TK_REVPIPE: {  /* '<|' 反向管道 */
        luaX_next(ls);
        expdesc e;
        /* 支持反向管道右侧直接使用字面量和匿名函数 */
        switch (ls->t.token) {
          case TK_FUNCTION: {  /* 匿名函数 */
            body(ls, &e, 0, ls->linenumber);
            break;
          }
          case TK_LAMBDA: {  /* lambda表达式 */
            lambda_body(ls, &e, ls->linenumber);
            break;
          }
          case TK_INT: {  /* 整数常量 */
            init_exp(&e, VKINT, 0);
            e.u.ival = ls->t.seminfo.i;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {  /* 浮点数常量 */
            init_exp(&e, VKFLT, 0);
            e.u.nval = ls->t.seminfo.r;
            luaX_next(ls);
            break;
          }
          case TK_STRING:
          case TK_RAWSTRING: {  /* 字符串常量 */
            codestring(&e, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {  /* true常量 */
            init_exp(&e, VTRUE, 0);
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {  /* false常量 */
            init_exp(&e, VFALSE, 0);
            luaX_next(ls);
            break;
          }
          case TK_NIL: {  /* nil常量 */
            init_exp(&e, VNIL, 0);
            luaX_next(ls);
            break;
          }
          case '{': {  /* 表常量作为参数 */
            constructor(ls, &e);
            break;
          }
          default: {
            /* 解析函数表达式（不递归处理管道，确保左关联） */
            pipe_funcexp(ls, &e);
            break;
          }
        }
        /* 生成反向管道运算符代码：v 是函数，e 是参数 */
        luaK_revpipe(fs, v, &e);
        break;
      }
      case TK_SAFEPIPE: {  /* '|?>' 安全管道 */
        /*
        ** 安全管道运算符: x |?> f
        ** 功能描述：如果 x 为 nil，则结果为 nil；否则结果为 f(x)
        ** 用于避免 nil 值导致的错误
        */
        luaX_next(ls);
        expdesc e;
        /* 支持管道符右侧直接使用字面量和匿名函数 */
        switch (ls->t.token) {
          case TK_FUNCTION: {  /* 匿名函数 */
            body(ls, &e, 0, ls->linenumber);
            break;
          }
          case TK_LAMBDA: {  /* lambda表达式 */
            lambda_body(ls, &e, ls->linenumber);
            break;
          }
          case TK_INT: {  /* 整数常量 */
            init_exp(&e, VKINT, 0);
            e.u.ival = ls->t.seminfo.i;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {  /* 浮点数常量 */
            init_exp(&e, VKFLT, 0);
            e.u.nval = ls->t.seminfo.r;
            luaX_next(ls);
            break;
          }
          case TK_STRING:
          case TK_RAWSTRING: {  /* 字符串常量 */
            codestring(&e, ls->t.seminfo.ts);
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {  /* true常量 */
            init_exp(&e, VTRUE, 0);
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {  /* false常量 */
            init_exp(&e, VFALSE, 0);
            luaX_next(ls);
            break;
          }
          case TK_NIL: {  /* nil常量 */
            init_exp(&e, VNIL, 0);
            luaX_next(ls);
            break;
          }
          case '{': {  /* 表常量作为函数 */
            constructor(ls, &e);
            break;
          }
          default: {
            /* 解析函数表达式（不递归处理管道，确保左关联） */
            pipe_funcexp(ls, &e);
            break;
          }
        }
        /* 生成安全管道运算符代码 */
        luaK_safepipe(fs, v, &e);
        break;
      }

      default: return;
    }
  }
}


static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> FLT | INT | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  switch (ls->t.token) {
    case TK_FLT: {
      init_exp(v, VKFLT, 0);
      v->u.nval = ls->t.seminfo.r;
      luaX_next(ls);
      break;
    }
    case TK_INT: {
      init_exp(v, VKINT, 0);
      v->u.ival = ls->t.seminfo.i;
      luaX_next(ls);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      luaX_next(ls);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      luaX_next(ls);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      luaX_next(ls);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->fs;
      check_condition(ls, fs->f->is_vararg,
                      "cannot use '...' outside a vararg function");
      init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 0, 1));
      luaX_next(ls);
      break;
    }
    case TK_FUNCTION: {
      luaX_next(ls);
      body(ls, v, 0, ls->linenumber);
      return;
    }
    case TK_LAMBDA: {
      luaX_next(ls);
      lambda_body(ls, v, ls->linenumber);
      return;
    }
    case TK_INTERPSTRING: {
      /*
      ** 字符串插值处理 - 统一语法版本
      ** 
      ** 语法规则:
      ** - ${name}    : 简单变量插值，直接引用变量
      ** - ${[expr]}  : 复杂表达式插值，方括号内是任意表达式
      ** - $$         : 输出字面量 $ (无需反斜杠转义)
      ** - $          : 后面不跟 { 或 $ 时，就是普通字符
      ** 
      ** 示例:
      **   local name = "World"
      **   local age = 25
      **   local msg1 = "Hello, ${name}!"           -- "Hello, World!"
      **   local msg2 = "Age: ${[age + 1]} years"   -- "Age: 26 years"
      **   local msg3 = "Price: $$100"              -- "Price: $100"
      **   local msg4 = "$${name}"                  -- "${name}" (字面量)
      **
      ** 实现原理:
      ** - ${name}: 直接查找变量，根据类型决定是否 tostring
      ** - ${[expr]}: 收集表达式中的局部变量，生成
      **   function(var1, var2, ...) return tostring(expr) end 并调用
      */
      TString *interp_str = ls->t.seminfo.ts;
      const char *str = getstr(interp_str);
      size_t len = tsslen(interp_str);
      FuncState *fs = ls->fs;
      
      luaX_next(ls);  /* 跳过字符串token */
      
      /* 检查是否有插值标记 */
      int has_interpolation = 0;
      size_t check_i;
      for (check_i = 0; check_i < len; check_i++) {
        if (str[check_i] == '$' && check_i + 1 < len && str[check_i + 1] == '{') {
          has_interpolation = 1;
          break;
        }
      }
      
      if (!has_interpolation) {
        /* 普通字符串，直接返回 */
        codestring(v, interp_str);
        break;
      }
      
      /* 有插值，收集所有片段到连续寄存器 */
      int base_reg = fs->freereg;  /* 第一个片段的寄存器 */
      int part_count = 0;          /* 片段数量 */
      size_t i = 0;
      size_t last_end = 0;
      
      while (i < len) {
        if (str[i] == '$' && i + 1 < len && str[i + 1] == '{') {
          /* 处理 ${...} 前面的字符串部分 */
          if (i > last_end) {
            TString *part_str = luaS_newlstr(ls->L, str + last_end, i - last_end);
            codestring(v, part_str);
            luaK_exp2nextreg(fs, v);
            part_count++;
          }
          
          i += 2;  /* 跳过 ${ */
          
          /* 检查是否是表达式模式 ${[expr]} */
          int is_expr_mode = (i < len && str[i] == '[');
          
          if (is_expr_mode) {
            i++;  /* 跳过 [ */
            size_t expr_start = i;
            int depth = 1;  /* [ ] 深度 */
            int brace_depth = 0;  /* { } 深度 */
            
            /* 找到匹配的 ]} (支持嵌套) */
            while (i < len && depth > 0) {
              if (str[i] == '[') depth++;
              else if (str[i] == ']') {
                depth--;
                if (depth == 0 && brace_depth == 0) break;
              }
              else if (str[i] == '{') brace_depth++;
              else if (str[i] == '}') brace_depth--;
              i++;
            }
            
            size_t expr_len = i - expr_start;
            i++;  /* 跳过 ] */
            if (i < len && str[i] == '}') i++;  /* 跳过 } */
            last_end = i;
            
            if (expr_len > 0) {
              /*
              ** 检查表达式是否只是一个简单的标识符
              ** 如果是，就像 ${name} 一样处理，不使用 load()
              */
              int is_simple_id = 1;
              size_t check_j;
              for (check_j = 0; check_j < expr_len; check_j++) {
                char c = str[expr_start + check_j];
                if (check_j == 0) {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                } else {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || c == '_')) {
                    is_simple_id = 0;
                    break;
                  }
                }
              }
              
              if (is_simple_id) {
                /*
                ** 简单标识符处理 - 和 ${name} 相同的逻辑
                */
                TString *varname = luaS_newlstr(ls->L, str + expr_start, expr_len);
                expdesc var_exp;
                
                int varkind = searchvar(fs, varname, &var_exp);
                if (varkind < 0) {
                  singlevaraux(fs, varname, &var_exp, 1);
                  /* 处理全局变量 */
                  if (var_exp.k == VVOID) {
                    expdesc key;
                    singlevaraux(fs, ls->envn, &var_exp, 1);
                    codestring(&key, varname);
                    luaK_indexed(fs, &var_exp, &key);
                  }
                }
                
                /* 调用 tostring */
                expdesc tostring_func;
                TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                singlevaraux(fs, tostring_name, &tostring_func, 1);
                if (tostring_func.k == VVOID) {
                  expdesc env_v;
                  singlevaraux(fs, ls->envn, &env_v, 1);
                  expdesc key;
                  codestring(&key, tostring_name);
                  luaK_indexed(fs, &env_v, &key);
                  tostring_func = env_v;
                }
                
                luaK_exp2nextreg(fs, &tostring_func);
                int call_reg = fs->freereg - 1;
                luaK_exp2nextreg(fs, &var_exp);
                luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                fs->freereg = call_reg + 1;
                part_count++;
              }
              else {
              /*
              ** 复杂表达式处理 - 支持访问局部变量
              ** 
              ** 实现原理:
              ** 1. 收集当前作用域的所有局部变量
              ** 2. 提取表达式中可能用到的变量名
              ** 3. 生成函数: function(var1, var2, ...) return tostring(expr) end
              ** 4. 用局部变量的值作为参数调用这个函数
              **
              ** 示例: age=25, "${[age + 1]}" 变成:
              **   (function(age) return tostring(age + 1) end)(age)
              */
              
              /* 收集表达式中的标识符 */
              #define MAX_INTERP_VARS 32
              TString *used_vars[MAX_INTERP_VARS];
              int var_regs[MAX_INTERP_VARS];  /* 变量在栈上的寄存器 */
              int nused = 0;
              
              /* 扫描表达式，提取所有标识符 */
              size_t scan_i = 0;
              while (scan_i < expr_len && nused < MAX_INTERP_VARS) {
                char c = str[expr_start + scan_i];
                /* 跳过字符串字面量 */
                if (c == '"' || c == '\'') {
                  char quote = c;
                  scan_i++;
                  while (scan_i < expr_len && str[expr_start + scan_i] != quote) {
                    if (str[expr_start + scan_i] == '\\') scan_i++;
                    scan_i++;
                  }
                  scan_i++;
                  continue;
                }
                /* 检查是否是标识符开始 */
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
                  size_t id_start = scan_i;
                  while (scan_i < expr_len) {
                    c = str[expr_start + scan_i];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_')) break;
                    scan_i++;
                  }
                  size_t id_len = scan_i - id_start;
                  
                  /* 检查是否是关键字（跳过） */
                  const char *id = str + expr_start + id_start;
                  int is_keyword = 0;
                  if (id_len == 3 && (strncmp(id, "and", 3) == 0 || 
                                      strncmp(id, "for", 3) == 0 ||
                                      strncmp(id, "not", 3) == 0 ||
                                      strncmp(id, "nil", 3) == 0 ||
                                      strncmp(id, "end", 3) == 0)) is_keyword = 1;
                  else if (id_len == 2 && (strncmp(id, "do", 2) == 0 ||
                                           strncmp(id, "if", 2) == 0 ||
                                           strncmp(id, "in", 2) == 0 ||
                                           strncmp(id, "or", 2) == 0)) is_keyword = 1;
                  else if (id_len == 4 && (strncmp(id, "then", 4) == 0 ||
                                           strncmp(id, "else", 4) == 0 ||
                                           strncmp(id, "true", 4) == 0)) is_keyword = 1;
                  else if (id_len == 5 && (strncmp(id, "while", 5) == 0 ||
                                           strncmp(id, "false", 5) == 0 ||
                                           strncmp(id, "local", 5) == 0 ||
                                           strncmp(id, "break", 5) == 0)) is_keyword = 1;
                  else if (id_len == 6 && (strncmp(id, "return", 6) == 0 ||
                                           strncmp(id, "repeat", 6) == 0)) is_keyword = 1;
                  else if (id_len == 8 && strncmp(id, "function", 8) == 0) is_keyword = 1;
                  
                  if (!is_keyword) {
                    TString *varname = luaS_newlstr(ls->L, id, id_len);
                    expdesc var_test;
                    
                    /* 检查是否是局部变量或上值 */
                    int varkind = searchvar(fs, varname, &var_test);
                    if (varkind >= 0) {
                      /* 是局部变量，记录下来 */
                      int already_added = 0;
                      int k;
                      for (k = 0; k < nused; k++) {
                        if (eqstr(used_vars[k], varname)) {
                          already_added = 1;
                          break;
                        }
                      }
                      if (!already_added && nused < MAX_INTERP_VARS) {
                        used_vars[nused] = varname;
                        var_regs[nused] = var_test.u.var.ridx;
                        nused++;
                      }
                    }
                  }
                }
                else {
                  scan_i++;
                }
              }
              
              /*
              ** 生成代码字符串:
              ** return tostring(expr)
              ** 使用 load() 编译后直接执行，不包装成函数
              */
              size_t code_prefix_len = 18;  /* "return tostring(" */
              size_t code_suffix_len = 1;   /* ")" */
              
              size_t total_len = code_prefix_len + expr_len + code_suffix_len;
              char *code_str = luaM_newblock(ls->L, total_len + 1);
              
              /* 构建代码字符串 */
              size_t pos = 0;
              memcpy(code_str + pos, "return tostring(", 16); pos += 16;
              memcpy(code_str + pos, str + expr_start, expr_len); pos += expr_len;
              memcpy(code_str + pos, ")", 1); pos += 1;
              code_str[pos] = '\0';
              
              /* 调用 load() 编译代码 */
              expdesc load_func;
              TString *load_name = luaS_newliteral(ls->L, "load");
              singlevaraux(fs, load_name, &load_func, 1);
              if (load_func.k == VVOID) {
                expdesc env_v;
                singlevaraux(fs, ls->envn, &env_v, 1);
                expdesc key;
                codestring(&key, load_name);
                luaK_indexed(fs, &env_v, &key);
                load_func = env_v;
              }
              
              int load_reg = fs->freereg;
              luaK_exp2nextreg(fs, &load_func);
              
              /* 参数1: 代码字符串 */
              TString *code_ts = luaS_newlstr(ls->L, code_str, pos);
              expdesc code_exp;
              codestring(&code_exp, code_ts);
              luaK_exp2nextreg(fs, &code_exp);
              
              /* 调用 load(code_str) 得到函数，只传一个参数让 load 使用默认环境 */
              luaK_codeABC(fs, OP_CALL, load_reg, 2, 2);
              fs->freereg = load_reg + 1;
              
              /* 调用返回的函数（无参数）*/
              luaK_codeABC(fs, OP_CALL, load_reg, 1, 2);
              fs->freereg = load_reg + 1;
              
              /* 移动结果到正确位置 */
              if (load_reg != base_reg + part_count) {
                luaK_codeABC(fs, OP_MOVE, base_reg + part_count, load_reg, 0);
                fs->freereg = base_reg + part_count + 1;
              }
              
              luaM_freearray(ls->L, code_str, total_len + 1);
              part_count++;
              }  /* end of else (complex expression) */
            }
          }
          else {
            /* 简单变量模式 ${name} */
            size_t expr_start = i;
            int depth = 1;
            
            /* 找到匹配的 } */
            while (i < len && depth > 0) {
              if (str[i] == '{') depth++;
              else if (str[i] == '}') depth--;
              if (depth > 0) i++;
            }
            
            size_t expr_len = i - expr_start;
            i++;  /* 跳过 } */
            last_end = i;
            
            if (expr_len > 0) {
              /* 检查是否是简单变量名 */
              int is_simple_var = 1;
              size_t j;
              for (j = 0; j < expr_len; j++) {
                char c = str[expr_start + j];
                if (j == 0) {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                    is_simple_var = 0;
                    break;
                  }
                } else {
                  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || c == '_')) {
                    is_simple_var = 0;
                    break;
                  }
                }
              }
              
              if (is_simple_var) {
                /*
                ** 简单变量名处理
                ** 直接查找变量，字符串/数字类型跳过 tostring
                */
                TString *varname = luaS_newlstr(ls->L, str + expr_start, expr_len);
                expdesc var_exp;
                
                int varkind = searchvar(fs, varname, &var_exp);
                if (varkind < 0) {
                  singlevaraux(fs, varname, &var_exp, 1);
                  /* 处理全局变量: 当 singlevaraux 返回 VVOID 时，需要通过 _ENV 访问 */
                  if (var_exp.k == VVOID) {
                    expdesc key;
                    singlevaraux(fs, ls->envn, &var_exp, 1);
                    codestring(&key, varname);
                    luaK_indexed(fs, &var_exp, &key);
                  }
                }
                
                /* 检查是否是编译期常量 */
                if (var_exp.k == VKSTR) {
                  /* 字符串常量，直接使用 */
                  luaK_exp2nextreg(fs, &var_exp);
                  part_count++;
                }
                else if (var_exp.k == VKINT || var_exp.k == VKFLT) {
                  /* 数字常量，需要转字符串 */
                  /* 获取 tostring 函数 */
                  expdesc tostring_func;
                  TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                  singlevaraux(fs, tostring_name, &tostring_func, 1);
                  if (tostring_func.k == VVOID) {
                    expdesc env_v;
                    singlevaraux(fs, ls->envn, &env_v, 1);
                    expdesc key;
                    codestring(&key, tostring_name);
                    luaK_indexed(fs, &env_v, &key);
                    tostring_func = env_v;
                  }
                  
                  luaK_exp2nextreg(fs, &tostring_func);
                  int call_reg = fs->freereg - 1;
                  luaK_exp2nextreg(fs, &var_exp);
                  luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                  fs->freereg = call_reg + 1;
                  part_count++;
                }
                else {
                  /* 运行时类型，调用 tostring */
                  expdesc tostring_func;
                  TString *tostring_name = luaS_newliteral(ls->L, "tostring");
                  singlevaraux(fs, tostring_name, &tostring_func, 1);
                  if (tostring_func.k == VVOID) {
                    expdesc env_v;
                    singlevaraux(fs, ls->envn, &env_v, 1);
                    expdesc key;
                    codestring(&key, tostring_name);
                    luaK_indexed(fs, &env_v, &key);
                    tostring_func = env_v;
                  }
                  
                  luaK_exp2nextreg(fs, &tostring_func);
                  int call_reg = fs->freereg - 1;
                  luaK_exp2nextreg(fs, &var_exp);
                  luaK_codeABC(fs, OP_CALL, call_reg, 2, 2);
                  fs->freereg = call_reg + 1;
                  part_count++;
                }
              }
              else {
                /* ${...} 内容不是有效变量名，报错或当作字面量处理 */
                TString *part_str = luaS_newlstr(ls->L, str + expr_start - 2, expr_len + 3);
                codestring(v, part_str);
                luaK_exp2nextreg(fs, v);
                part_count++;
              }
            }
          }
        }
        else {
          i++;
        }
      }
      
      /* 处理最后的字符串部分 */
      if (last_end < len) {
        TString *part_str = luaS_newlstr(ls->L, str + last_end, len - last_end);
        codestring(v, part_str);
        luaK_exp2nextreg(fs, v);
        part_count++;
      }
      
      /* 使用 OP_CONCAT 连接所有片段 */
      if (part_count == 0) {
        TString *empty_str = luaS_newliteral(ls->L, "");
        codestring(v, empty_str);
      }
      else if (part_count == 1) {
        init_exp(v, VNONRELOC, base_reg);
      }
      else {
        /* OP_CONCAT A B C: R[A] := R[A] .. ... .. R[A + B - 1] */
        luaK_codeABC(fs, OP_CONCAT, base_reg, part_count, 0);
        fs->freereg = base_reg + 1;
        init_exp(v, VNONRELOC, base_reg);
      }
      
      v->t = NO_JUMP;
      v->f = NO_JUMP;
      return;
    }
    case TK_SWITCH: {
      /**
       * switch表达式语法糖 - 将switch作为表达式使用
       * 转换为立即执行函数（IIFE）:
       *   a = switch (exp) do case... end
       * 等价于:
       *   a = (function() switch (exp) do case... end end)()
       * 
       * 这样switch内部的return语句就能正确返回值给外部变量
       */
      int line = ls->linenumber;
      FuncState new_fs;
      BlockCnt bl;
      FuncState *fs = ls->fs;
      
      /* 创建新的函数原型 */
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);
      
      /* 解析switch语句作为函数体 */
      switchstat(ls, line);
      
      new_fs.f->lastlinedefined = ls->linenumber;
      
      /* 生成闭包 */
      codeclosure(ls, v);
      close_func(ls);
      
      /* 立即调用这个闭包（无参数调用） */
      luaK_exp2nextreg(fs, v);
      int base = v->u.info;
      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, base, 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = base + 1;
      return;
    }
    case TK_ARROW: {
      /**
       * 箭头函数语法糖（语句形式）: ->(args){ stat } 或 ->{ stat }
       * 等价于: function(args) stat end
       * 
       * 语法说明：
       *   funcA = ->(arg1,...){ stat }  -- 带参数的匿名函数
       *   funcA2 = ->{ stat }           -- 无参数的匿名函数
       */
      int line = ls->linenumber;
      FuncState new_fs;
      BlockCnt bl;
      luaX_next(ls);  /* 跳过 '->' */
      
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);
      
      /* 解析参数列表（可选） */
      if (testnext(ls, '(')) {
        parlist(ls);
        checknext(ls, ')');
      }
      
      /* 解析函数体 { stat } */
      checknext(ls, '{');
      while (ls->t.token != '}' && ls->t.token != TK_EOS) {
        if (ls->t.token == TK_RETURN) {
          statement(ls);
          break;
        }
        statement(ls);
      }
      check_match(ls, '}', '{', line);
      
      new_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, v);
      close_func(ls);
      return;
    }
    case TK_MEAN: {
      /**
       * 箭头函数语法糖（表达式形式）: =>(args){ exp } 或 =>{ exp }
       * 等价于: function(args) return exp end
       * 
       * 语法说明：
       *   funcB = =>(arg1,...){ exp }  -- 带参数，自动返回表达式
       *   funcB2 = =>{ exp }           -- 无参数，自动返回表达式
       */
      int line = ls->linenumber;
      FuncState new_fs;
      BlockCnt bl;
      luaX_next(ls);  /* 跳过 '=>' */
      
      new_fs.f = addprototype(ls);
      new_fs.f->linedefined = line;
      open_func(ls, &new_fs, &bl);
      
      /* 解析参数列表（可选） */
      if (testnext(ls, '(')) {
        parlist(ls);
        checknext(ls, ')');
      }
      
      /* 解析函数体 { exp } - 自动返回表达式 */
      checknext(ls, '{');
      enterlevel(ls);
      retstat(ls);  /* 将表达式作为return语句处理 */
      lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
                 ls->fs->freereg >= ls->fs->nactvar);
      ls->fs->freereg = ls->fs->nactvar;
      leavelevel(ls);
      check_match(ls, '}', '{', line);
      
      new_fs.f->lastlinedefined = ls->linenumber;
      codeclosure(ls, v);
      close_func(ls);
      return;
    }
    case '[': {
      /**
       * 条件测试表达式语法: [ test_expr ]
       * 类似Shell的条件测试，支持：
       *   - 文件测试: [ -f "path" ], [ -d "path" ], [ -e "path" ] 等
       *   - 数值比较: [ a -eq b ], [ a -lt b ] 等
       *   - 字符串比较: [ str1 = str2 ], [ -z str ] 等
       *   - Lua类型测试: [ -type var "table" ], [ -nil var ] 等
       *   - 逻辑运算: [ cond1 -a cond2 ], [ ! cond ] 等
       *
       * 编译为: __test__(arg1, arg2, ...)
       */
      FuncState *fs = ls->fs;
      int line = ls->linenumber;
      expdesc func;
      int base;
      int nargs = 0;
      
      luaX_next(ls);  /* 跳过 '[' */
      
      /* 获取 __test__ 函数 */
      singlevaraux(fs, luaS_newliteral(ls->L, "__test__"), &func, 1);
      if (func.k == VVOID) {
        /* 如果不存在，从全局表获取 */
        expdesc key;
        singlevaraux(fs, ls->envn, &func, 1);
        codestring(&key, luaS_newliteral(ls->L, "__test__"));
        luaK_indexed(fs, &func, &key);
      }
      luaK_exp2nextreg(fs, &func);
      base = func.u.info;
      
      /* 解析条件测试表达式参数 */
      while (ls->t.token != ']' && ls->t.token != TK_EOS) {
        expdesc arg;
        
        /* 处理逻辑非操作符 (! 或 not) */
        if (ls->t.token == '!' || ls->t.token == TK_NOT) {
          codestring(&arg, luaS_newliteral(ls->L, "!"));
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          luaX_next(ls);
          continue;
        }
        
        /* 处理带 - 前缀的操作符（如 -f, -eq, -type 等）*/
        if (ls->t.token == '-') {
          luaX_next(ls);
          if (ls->t.token == TK_NAME) {
            /* 构造操作符字符串 "-xxx" */
            TString *op_name = ls->t.seminfo.ts;
            const char *name = getstr(op_name);
            size_t len = tsslen(op_name);
            char *buf = luaM_newvector(ls->L, len + 2, char);
            buf[0] = '-';
            memcpy(buf + 1, name, len);
            buf[len + 1] = '\0';
            TString *op_str = luaS_newlstr(ls->L, buf, len + 1);
            luaM_freearray(ls->L, buf, len + 2);
            codestring(&arg, op_str);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            continue;
          } else if (ls->t.token == TK_INT) {
            /* 负数 */
            init_exp(&arg, VKINT, 0);
            arg.u.ival = -ls->t.seminfo.i;
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            continue;
          } else if (ls->t.token == TK_FLT) {
            /* 负浮点数 */
            init_exp(&arg, VKFLT, 0);
            arg.u.nval = -ls->t.seminfo.r;
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            continue;
          } else {
            luaX_syntaxerror(ls, "expected operator name after '-' in test expression");
          }
        }
        
        /* 处理比较操作符 */
        if (ls->t.token == '=') {
          codestring(&arg, luaS_newliteral(ls->L, "="));
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          luaX_next(ls);
          continue;
        }
        if (ls->t.token == TK_EQ) {  /* == */
          codestring(&arg, luaS_newliteral(ls->L, "=="));
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          luaX_next(ls);
          continue;
        }
        if (ls->t.token == TK_NE) {  /* ~= 或 != */
          codestring(&arg, luaS_newliteral(ls->L, "!="));
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          luaX_next(ls);
          continue;
        }
        
        /* 处理模式匹配操作符 =~ 和 !~ */
        if (ls->t.token == '~') {
          luaX_next(ls);
          if (ls->t.token == '=') {
            codestring(&arg, luaS_newliteral(ls->L, "=~"));
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            continue;
          }
          /* 其他情况回退处理 */
          codestring(&arg, luaS_newliteral(ls->L, "~"));
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          continue;
        }
        
        /* 处理括号表达式 - 这里使用完整表达式解析 */
        if (ls->t.token == '(') {
          luaX_next(ls);
          expr(ls, &arg);
          checknext(ls, ')');
          luaK_exp2nextreg(fs, &arg);
          nargs++;
          continue;
        }
        
        /* 处理简单值：字符串、数字、布尔值、nil、变量名 */
        /* 不使用 expr() 以避免解析后续的 -a/-o 等操作符 */
        switch (ls->t.token) {
          case TK_STRING:
          case TK_INTERPSTRING:
          case TK_RAWSTRING: {
            codestring(&arg, ls->t.seminfo.ts);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_INT: {
            init_exp(&arg, VKINT, 0);
            arg.u.ival = ls->t.seminfo.i;
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_FLT: {
            init_exp(&arg, VKFLT, 0);
            arg.u.nval = ls->t.seminfo.r;
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_TRUE: {
            init_exp(&arg, VTRUE, 0);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_FALSE: {
            init_exp(&arg, VFALSE, 0);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_NIL: {
            init_exp(&arg, VNIL, 0);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            luaX_next(ls);
            break;
          }
          case TK_NAME: {
            /* 变量名 - 解析为变量引用 */
            singlevar(ls, &arg);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            break;
          }
          case '{': {
            /* 表构造器 */
            constructor(ls, &arg);
            luaK_exp2nextreg(fs, &arg);
            nargs++;
            break;
          }
          default: {
            luaX_syntaxerror(ls, "unexpected token in test expression");
          }
        }
      }
      
      check_match(ls, ']', '[', line);
      
      /* 生成函数调用 */
      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, base, nargs + 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = base + 1;
      return;
    }
    default: {
      suffixedexp(ls, v);
      return;
    }
  }
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case '/': return OPR_DIV;
    case TK_IDIV: return OPR_IDIV;
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case '~': return OPR_BXOR;
    case TK_SHL: return OPR_SHL;
    case TK_SHR: return OPR_SHR;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_PIPE: return OPR_PIPE;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_SPACESHIP: return OPR_SPACESHIP;
    case TK_IS: return OPR_IS;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    case TK_IN: return OPR_IN;
    case TK_NULLCOAL: return OPR_NULLCOAL;
    default: return OPR_NOBINOPR;
  }
}


/*
** Priority table for binary operators.
*/
static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                  /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},   /* '&' '|' '~' */
   {7, 7}, {7, 7},           /* '<<' '>>' */
   {9, 8},                   /* '..' (right associative) */
   {8, 7},                   /* '|>' (right associative) */
   {3, 3}, {3, 3}, {3, 3},   /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},   /* ~=, >, >= */
   {3, 3},                   /* <=> (spaceship) */
   {3, 3},                   /* is */
   {3, 3},                   /* in */
   {2, 2}, {1, 1},           /* and, or */
   {1, 1}                    /* ?? (null coalescing, right associative) */
};

#define UNARY_PRIORITY	12  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    subexpr(ls, v, UNARY_PRIORITY);
    luaK_prefix(ls->fs, uop, v, line);
  }
  else simpleexp(ls, v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    luaK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(ls, &v2, priority[op].right);
    luaK_posfix(ls->fs, op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}


/*
** 条件表达式专用的 suffixedexp
** 与 suffixedexp 相同，但不将 '{' 作为函数调用参数
** 这样在 if cond {} 语法中，{} 会被正确解析为代码块而非函数调用
*/
static void cond_suffixedexp (LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  primaryexp(ls, v);
  for (;;) {
    switch (ls->t.token) {
      case TK_OPTCHAIN: {  /* '?.' 可选链字段访问 */
        expdesc key;
        int reg;
        int jmp_skip;
        int idx;
        
        luaK_dischargevars(fs, v);
        reg = luaK_exp2anyreg(fs, v);
        
        luaK_codeABCk(fs, OP_TESTNIL, reg, reg, 0, 1);
        jmp_skip = luaK_jump(fs);
        
        luaX_next(ls);  /* 跳过 '?.' */
        
        if (ls->t.token == TK_NAME) {
          codename(ls, &key);
        }
        else {
          TString *ts;
          switch (ls->t.token) {
            case TK_AND: ts = luaS_newliteral(ls->L, "and"); break;
            case TK_BREAK: ts = luaS_newliteral(ls->L, "break"); break;
            case TK_CASE: ts = luaS_newliteral(ls->L, "case"); break;
            case TK_CATCH: ts = luaS_newliteral(ls->L, "catch"); break;
            case TK_COMMAND: ts = luaS_newliteral(ls->L, "command"); break;
            case TK_CONST: ts = luaS_newliteral(ls->L, "const"); break;
            case TK_CONTINUE: ts = luaS_newliteral(ls->L, "continue"); break;
            case TK_DEFAULT: ts = luaS_newliteral(ls->L, "default"); break;
            case TK_DO: ts = luaS_newliteral(ls->L, "do"); break;
            case TK_ELSE: ts = luaS_newliteral(ls->L, "else"); break;
            case TK_ELSEIF: ts = luaS_newliteral(ls->L, "elseif"); break;
            case TK_END: ts = luaS_newliteral(ls->L, "end"); break;
            case TK_ENUM: ts = luaS_newliteral(ls->L, "enum"); break;
            case TK_FALSE: ts = luaS_newliteral(ls->L, "false"); break;
            case TK_FINALLY: ts = luaS_newliteral(ls->L, "finally"); break;
            case TK_FOR: ts = luaS_newliteral(ls->L, "for"); break;
            case TK_FUNCTION: ts = luaS_newliteral(ls->L, "function"); break;
            case TK_GLOBAL: ts = luaS_newliteral(ls->L, "global"); break;
            case TK_GOTO: ts = luaS_newliteral(ls->L, "goto"); break;
            case TK_IF: ts = luaS_newliteral(ls->L, "if"); break;
            case TK_IN: ts = luaS_newliteral(ls->L, "in"); break;
            case TK_IS: ts = luaS_newliteral(ls->L, "is"); break;
            case TK_LAMBDA: ts = luaS_newliteral(ls->L, "lambda"); break;
            case TK_LOCAL: ts = luaS_newliteral(ls->L, "local"); break;
            case TK_NIL: ts = luaS_newliteral(ls->L, "nil"); break;
            case TK_NOT: ts = luaS_newliteral(ls->L, "not"); break;
            case TK_OR: ts = luaS_newliteral(ls->L, "or"); break;
            case TK_REPEAT: ts = luaS_newliteral(ls->L, "repeat"); break;
            case TK_RETURN: ts = luaS_newliteral(ls->L, "return"); break;
            case TK_SWITCH: ts = luaS_newliteral(ls->L, "switch"); break;
            case TK_TAKE: ts = luaS_newliteral(ls->L, "take"); break;
            case TK_THEN: ts = luaS_newliteral(ls->L, "then"); break;
            case TK_TRUE: ts = luaS_newliteral(ls->L, "true"); break;
            case TK_TRY: ts = luaS_newliteral(ls->L, "try"); break;
            case TK_UNTIL: ts = luaS_newliteral(ls->L, "until"); break;
            case TK_WHEN: ts = luaS_newliteral(ls->L, "when"); break;
            case TK_WITH: ts = luaS_newliteral(ls->L, "with"); break;
            case TK_WHILE: ts = luaS_newliteral(ls->L, "while"); break;
            case TK_KEYWORD: ts = luaS_newliteral(ls->L, "keyword"); break;
            case TK_OPERATOR: ts = luaS_newliteral(ls->L, "operator"); break;
            default: error_expected(ls, TK_NAME);
          }
          codestring(&key, ts);
          luaX_next(ls);
        }
        
        v->k = VNONRELOC;
        v->u.info = reg;
        luaK_indexed(fs, v, &key);
        idx = v->u.ind.idx;
        
        luaK_codeABC(fs, OP_GETFIELD, reg, reg, idx);
        
        luaK_patchtohere(fs, jmp_skip);
        
        v->k = VNONRELOC;
        v->u.info = reg;
        v->t = NO_JUMP;
        v->f = NO_JUMP;
        break;
      }
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' 或切片语法 */
        yindex_or_slice(ls, v);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        luaX_next(ls);
        codename(ls, &key);
        luaK_self(fs, v, &key);
        funcargs(ls, v, line);
        break;
      }
      case '(': case TK_STRING: case TK_RAWSTRING: {  /* funcargs - 注意：不包含 '{' */
        luaK_exp2nextreg(fs, v);
        funcargs(ls, v, line);
        break;
      }
      /* 注意：条件表达式中不处理 '{' 作为函数调用，
         这样 if cond {} 中的 {} 会被识别为代码块 */
      default: return;  /* 遇到其他 token（包括 '{'）就停止 */
    }
  }
}


/*
** 条件表达式专用的 simpleexp
** 与 simpleexp 相同，但使用 cond_suffixedexp
*/
static void cond_simpleexp (LexState *ls, expdesc *v) {
  switch (ls->t.token) {
    case TK_FLT: {
      init_exp(v, VKFLT, 0);
      v->u.nval = ls->t.seminfo.r;
      luaX_next(ls);
      break;
    }
    case TK_INT: {
      init_exp(v, VKINT, 0);
      v->u.ival = ls->t.seminfo.i;
      luaX_next(ls);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      luaX_next(ls);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      luaX_next(ls);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      luaX_next(ls);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->fs;
      check_condition(ls, fs->f->is_vararg,
                      "cannot use '...' outside a vararg function");
      init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 0, 1));
      luaX_next(ls);
      break;
    }
    case TK_STRING:
    case TK_RAWSTRING: {
      codestring(v, ls->t.seminfo.ts);
      luaX_next(ls);
      break;
    }
    default: {
      cond_suffixedexp(ls, v);
      return;
    }
  }
}


/*
** 条件表达式专用的 subexpr
** 与 subexpr 相同，但使用 cond_simpleexp
*/
static BinOpr cond_subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    cond_subexpr(ls, v, UNARY_PRIORITY);
    luaK_prefix(ls->fs, uop, v, line);
  }
  else cond_simpleexp(ls, v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;
    luaX_next(ls);  /* skip operator */
    luaK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    nextop = cond_subexpr(ls, &v2, priority[op].right);
    luaK_posfix(ls->fs, op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


/*
** 条件表达式解析
** 用于 if/while/until 等控制结构的条件部分
** 与 expr 的区别是不将 '{' 作为函数调用参数
** 这样 if cond {} 语法中的 {} 会被正确解析为代码块
*/
static void cond_expr (LexState *ls, expdesc *v) {
  cond_subexpr(ls, v, 0);
}

/* }========================================================= */



/*
** {===========================================================
** Rules for Statements
** ============================================================
*/


static void block (LexState *ls) {
  /* block -> statlist */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  statlist(ls);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  lu_byte extra = fs->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (vkisindexed(lh->v.k)) {  /* assignment to table field? */
      if (lh->v.k == VINDEXUP) {  /* is table an upvalue? */
        if (v->k == VUPVAL && lh->v.u.ind.t == v->u.info) {
          conflict = 1;  /* table is the upvalue being assigned now */
          lh->v.k = VINDEXSTR;
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
      }
      else {  /* table is a register */
        if (v->k == VLOCAL && lh->v.u.ind.t == v->u.var.ridx) {
          conflict = 1;  /* table is the local being assigned now */
          lh->v.u.ind.t = extra;  /* assignment will use safe copy */
        }
        /* is index the local being assigned? */
        if (lh->v.k == VINDEXED && v->k == VLOCAL &&
            lh->v.u.ind.idx == v->u.var.ridx) {
          conflict = 1;
          lh->v.u.ind.idx = extra;  /* previous assignment will use safe copy */
        }
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    if (v->k == VLOCAL)
      luaK_codeABC(fs, OP_MOVE, extra, v->u.var.ridx, 0);
    else
      luaK_codeABC(fs, OP_GETUPVAL, extra, v->u.info, 0);
    luaK_reserveregs(fs, 1);
  }
}


/* Create code to store the "top" register in 'var' */
static void storevartop (FuncState *fs, expdesc *var) {
  expdesc e;
  init_exp(&e, VNONRELOC, fs->freereg - 1);
  luaK_storevar(fs, var, &e);  /* will also free the top register */
}


/*
** Parse and compile a multiple assignment. The first "variable"
** (a 'suffixedexp') was already read by the caller.
**
** assignment -> suffixedexp restassign
** restassign -> ',' suffixedexp restassign | '=' explist
*/
static void restassign (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(ls, vkisvar(lh->v.k), "syntax error");
  check_readonly(ls, &lh->v);
  if (testnext(ls, ',')) {  /* restassign -> ',' suffixedexp restassign */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(ls, &nv.v);
    if (!vkisindexed(nv.v.k))
      check_conflict(ls, lh, &nv.v);
    enterlevel(ls);  /* control recursion depth */
    restassign(ls, &nv, nvars+1);
    leavelevel(ls);
  }
  else {  /* restassign -> '=' explist */
    int nexps;
    checknext(ls, '=');
    nexps = explist(ls, &e);
    if (nexps != nvars)
      adjust_assign(ls, nvars, nexps, &e);
    else {
      luaK_setoneret(ls->fs, &e);  /* close last expression */
      luaK_storevar(ls->fs, &lh->v, &e);
      return;  /* avoid default */
    }
  }
  init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
  luaK_storevar(ls->fs, &lh->v, &e);
}


static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  expr(ls, &v);  /* read condition */
  if (v.k == VNIL) v.k = VFALSE;  /* 'falses' are all equal here */
  luaK_goiftrue(ls->fs, &v);
  return v.f;
}


static void gotostat (LexState *ls) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  if (ls->t.token == TK_CONTINUE) {
    luaX_next(ls);
    breakstat(ls);
    return;
  }
  TString *name = str_checkname(ls);  /* label's name */
  Labeldesc *lb = findlabel(ls, name);
  if (lb == NULL)  /* no label? */
    /* forward jump; will be resolved when the label is declared */
    newgotoentry(ls, name, line, luaK_jump(fs));
  else {  /* found a label */
    /* backward jump; will be resolved here */
    int lblevel = reglevel(fs, lb->nactvar);  /* label level */
    if (luaY_nvarstack(fs) > lblevel)  /* leaving the scope of a variable? */
      luaK_codeABC(fs, OP_CLOSE, lblevel, 0, 0);
    /* create jump and link it to the label */
    luaK_patchlist(fs, luaK_jump(fs), lb->pc);
  }
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
static void breakstat (LexState *ls) {
  int line = ls->linenumber;
  int temp = ls->t.token;
  luaX_next(ls);  /* skip break */
  if(temp==TK_BREAK) {
      newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, luaK_jump(ls->fs));
  }else if(temp==TK_CONTINUE){
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, luaK_jump(ls->fs));
  }
}

/*
** Check whether there is already a label with the given 'name'.
*/
static void checkrepeated (LexState *ls, TString *name) {
  Labeldesc *lb = findlabel(ls, name);
  if (l_unlikely(lb != NULL)) {  /* already defined? */
    const char *msg = "label '%s' already defined on line %d";
    msg = luaO_pushfstring(ls->L, msg, getstr(name), lb->line);
    luaK_semerror(ls, msg);  /* error */
  }
}


static void labelstat (LexState *ls, TString *name, int line) {
  /* label -> '::' NAME '::' */
  checknext(ls, TK_DBCOLON);  /* skip double colon */
  while (ls->t.token == ';' || ls->t.token == TK_DBCOLON)
    statement(ls);  /* skip other no-op statements */
  checkrepeated(ls, name);  /* check for repeated labels */
  createlabel(ls, name, line, block_follow(ls, 0));
}


static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  luaX_next(ls);  /* skip WHILE */
  whileinit = luaK_getlabel(fs);
  condexit = cond(ls);
  enterblock(fs, &bl, 1);
  if (ls->t.token == TK_DO) luaX_next(ls);
  block(ls);
  createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
  luaK_jumpto(fs, whileinit);
  check_match(ls, TK_END, TK_WHILE, line);
  leaveblock(fs);
  luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  luaX_next(ls);  /* skip REPEAT */
  statlist(ls);
  createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
  check_match(ls, TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  leaveblock(fs);  /* finish scope */
  if (bl2.upval) {  /* upvalues? */
    int exit = luaK_jump(fs);  /* normal exit must jump over fix */
    luaK_patchtohere(fs, condexit);  /* repetition must close upvalues */
    luaK_codeABC(fs, OP_CLOSE, reglevel(fs, bl2.nactvar), 0, 0);
    condexit = luaK_jump(fs);  /* repeat after closing upvalues */
    luaK_patchtohere(fs, exit);  /* normal exit comes to here */
  }
  luaK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  leaveblock(fs);  /* finish loop */
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
static void exp1 (LexState *ls) {
  expdesc e;
  expr(ls, &e);
  luaK_exp2nextreg(ls->fs, &e);
  lua_assert(e.k == VNONRELOC);
}


/*
** Fix for instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
static void fixforjump (FuncState *fs, int pc, int dest, int back) {
  Instruction *jmp = &fs->f->code[pc];
  int offset = dest - (pc + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    luaX_syntaxerror(fs->ls, "control structure too long");
  SETARG_Bx(*jmp, offset);
}


/*
** Generate code for a 'for' loop.
*/
static void forbody (LexState *ls, int base, int line, int nvars, int isgen) {
  /* forbody -> DO block */
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  if (ls->t.token == TK_DO) luaX_next(ls);
  prep = luaK_codeABx(fs, forprep[isgen], base, 0);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  luaK_reserveregs(fs, nvars);
  block(ls);
  createlabel(ls, luaS_newliteral(ls->L, "continue"), 0, 0);
  leaveblock(fs);  /* end of scope for declared variables */
  fixforjump(fs, prep, luaK_getlabel(fs), 0);
  if (isgen) {  /* generic for? */
    luaK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
    luaK_fixline(fs, line);
  }
  endfor = luaK_codeABx(fs, forloop[isgen], base, 0);
  fixforjump(fs, endfor, prep + 1, 1);
  luaK_fixline(fs, line);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp,exp[,exp] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_varkind(ls, varname, RDKCONST);  /* 控制变量设为只读常量 */
  checknext(ls, '=');
  exp1(ls);  /* initial value */
  checknext(ls, ',');
  exp1(ls);  /* limit */
  if (testnext(ls, ','))
    exp1(ls);  /* optional step */
  else {  /* default step = 1 */
    luaK_int(fs, fs->freereg, 1);
    luaK_reserveregs(fs, 1);
  }
  adjustlocalvars(ls, 3);  /* control variables */
  forbody(ls, base, line, 1, 0);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 5;  /* gen, state, control, toclose, 'indexname' */
  int line;
  int base = fs->freereg;
  /* create control variables */
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  /* create declared variables */
  new_varkind(ls, indexname, RDKCONST);  /* 控制变量设为只读常量 */
  while (testnext(ls, ',')) {
    new_localvar(ls, str_checkname(ls));
    nvars++;
  }
  if (ls->t.token == TK_IN) luaX_next(ls);
  line = ls->linenumber;
  adjust_assign(ls, 4, explist(ls, &e), &e);
  adjustlocalvars(ls, 4);  /* control variables */
  marktobeclosed(fs);  /* last control var. must be closed */
  luaK_checkstack(fs, 3);  /* extra space to call generator */
  forbody(ls, base, line, nvars - 4, 1);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  luaX_next(ls);  /* skip 'for' */
  varname = str_checkname(ls);  /* first variable name */
  switch (ls->t.token) {
    case '=': fornum(ls, varname, line); break;
    case ',': case TK_IN: forlist(ls, varname); break;
    default: luaX_syntaxerror(ls, "'=' or 'in' expected");
  }
  check_match(ls, TK_END, TK_FOR, line);
  leaveblock(fs);  /* loop scope ('break' jumps to this point) */
}


/*
** 解析 if/elseif 条件块
** 语法支持两种形式：
**   1. 传统形式: if cond then block
**   2. 大括号形式: if cond { block }
** 
** 参数：
**   ls - 词法状态
**   escapelist - 跳出列表
** 返回值：
**   1 如果使用大括号语法，0 否则
*/
static int test_then_block (LexState *ls, int *escapelist) {
  /* test_then_block -> [IF | ELSEIF] cond [THEN | '{'] block ['}'] */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  expdesc v;
  int jf;  /* instruction to skip 'then' code (if condition is false) */
  int use_brace = 0;  /* 是否使用大括号语法 */
  luaX_next(ls);  /* skip IF or ELSEIF */
  cond_expr(ls, &v);  /* read condition (使用 cond_expr 避免 { 被误解为函数调用) */
  
  /* 检查是否是大括号语法 */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* skip '{' */
  } else if (ls->t.token == TK_THEN) {
    luaX_next(ls);  /* skip 'then' */
  }
  
  if (ls->t.token == TK_BREAK||ls->t.token==TK_CONTINUE) {  /* 'if x then break' ? */
    int line = ls->linenumber;
    luaK_goiffalse(ls->fs, &v);  /* will jump if condition is true */
    if(ls->t.token==TK_BREAK) {
      luaX_next(ls);  /* skip 'break' */
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, v.t);
    }else{
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, v.t);
    }
    while (testnext(ls, ';')) {}  /* skip semicolons */
    if (block_follow(ls, 0) || (use_brace && ls->t.token == '}')) {  /* jump is the entire block? */
      leaveblock(fs);
      if (use_brace) checknext(ls, '}');
      return use_brace;  /* and that is it */
    }
    else  /* must skip over 'then' part if condition is false */
      jf = luaK_jump(fs);
  }
  else {  /* regular case (not a break) */
    luaK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
    enterblock(fs, &bl, 0);
    jf = v.f;
  }
  
  /* 解析块内容 */
  if (use_brace) {
    /* 大括号语法：解析到 '}' 结束 */
    while (ls->t.token != '}' && ls->t.token != TK_EOS) {
      statement(ls);
    }
    checknext(ls, '}');
  } else {
    statlist(ls);  /* 'then' part */
  }
  
  leaveblock(fs);
  if (ls->t.token == TK_ELSE ||
      ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
  luaK_patchtohere(fs, jf);
  return use_brace;
}


/*
** if 语句解析
** 支持两种语法形式：
**   1. 传统形式: if cond then block {elseif cond then block} [else block] end
**   2. 大括号形式: if cond { block } {elseif cond { block }} [else { block }]
** 
** 参数：
**   ls - 词法状态
**   line - if 关键字所在行号
*/
static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond [THEN|'{'] block {ELSEIF cond [THEN|'{'] block} [ELSE ['{'] block ['}']] [END] */
  FuncState *fs = ls->fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  int use_brace = test_then_block(ls, &escapelist);  /* IF cond THEN block */
  
  while (ls->t.token == TK_ELSEIF) {
    int elseif_brace = test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
    use_brace = use_brace || elseif_brace;
  }
  
  if (testnext(ls, TK_ELSE)) {
    /* else 部分 */
    if (use_brace && ls->t.token == '{') {
      /* else { block } */
      luaX_next(ls);  /* skip '{' */
      while (ls->t.token != '}' && ls->t.token != TK_EOS) {
        statement(ls);
      }
      checknext(ls, '}');
    } else {
      block(ls);  /* 'else' part */
    }
  }
  
  /* 只有传统语法需要 end */
  if (!use_brace) {
    check_match(ls, TK_END, TK_IF, line);
  }
  
  luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void single_test_then_block (LexState *ls, int *escapelist) {
    /* test_then_block -> [IF | ELSEIF] cond THEN block */
    BlockCnt bl;
    int line;
    FuncState *fs = ls->fs;
    TString *jlb = NULL;
    int target = NO_JUMP;
    expdesc v;
    int jf;  /* instruction to skip 'then' code (if condition is false) */
    luaX_next(ls);  /* skip IF or ELSEIF */
    cond_expr(ls, &v);  /* read condition (使用 cond_expr 避免 { 被误解为函数调用) */
    line = ls->linenumber;
    if (ls->t.token == TK_GOTO || ls->t.token == TK_BREAK || ls->t.token == TK_CONTINUE) {
        luaK_goiffalse(ls->fs, &v);  /* will jump to label if condition is true */
        enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
        gotostat(ls);  /* handle goto/break */
        leaveblock(fs);
        return;
    }
    else {  /* regular case (not a jump) */
        luaK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
        enterblock(fs, &bl, 0);
        jf = v.f;
    }
    statement(ls);
    leaveblock(fs);
    if (ls->t.token == TK_ELSE || ls->t.token == TK_CASE ||
        ls->t.token == TK_WHEN)  /* followed by 'else'/'elseif'? */
        luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
    luaK_patchtohere(fs, jf);
}

static void single_block (LexState *ls) {
    /* block -> statlist */
    FuncState *fs = ls->fs;
    BlockCnt bl;
    enterblock(fs, &bl, 0);
    statement(ls);
    leaveblock(fs);
}


static void single_ifstat (LexState *ls, int line) {
    /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    FuncState *fs = ls->fs;
    int escapelist = NO_JUMP;  /* exit list for finished parts */
    single_test_then_block(ls, &escapelist);  /* IF cond THEN block */
    if (testnext(ls,'`'))
        single_block(ls);  /* 'else' part */
    luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


static void whenstat (LexState *ls, int line) {
    /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
    FuncState *fs = ls->fs;
    int escapelist = NO_JUMP;  /* exit list for finished parts */
    single_test_then_block(ls, &escapelist);  /* IF cond THEN block */
    while (ls->t.token == TK_CASE)
        single_test_then_block(ls, &escapelist);  /* IF cond THEN block */
    if (testnext(ls, TK_ELSE))
        single_block(ls);  /* 'else' part */
    luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
}


//===================================== SWITCH =============================================
static expdesc clone(expdesc e2){
  expdesc e1;
  e1.f=e2.f;
  e1.k=e2.k;
  e1.t=e2.t;
  e1.u.ind.t=e2.u.ind.t;
  e1.u.ind.idx=e2.u.ind.idx;
  e1.u.var.vidx=e2.u.var.vidx;
  e1.u.var.ridx=e2.u.var.ridx;
  e1.u.info=e2.u.info;
  e1.u.nval=e2.u.nval;
  e1.u.ival=e2.u.ival;
  e1.u.strval=e2.u.strval;
  return e1;
}

static void switch_read_var (LexState *ls, expdesc *v) {
    enterlevel(ls);
    subexpr(ls, v, 0);
    //simpleexp(ls, v);
    leavelevel(ls);
}

static void test_case_block (LexState *ls, int *escapelist, expdesc *control) {
    /* test_case_block -> [CASE] var DO block */
    BlockCnt bl;
    FuncState *fs = ls->fs;
    int jf;  /* instruction to skip 'case' code (if condition is false) */
    luaX_next(ls);  /* skip CASE */
    expdesc v;
    expdesc gv = clone(*control);
    enterlevel(ls);
    cond_expr(ls, &v);  /* 使用 cond_expr 避免 { 被误解为函数调用 */
    luaK_posfix(ls->fs, OPR_EQ, control, &v, ls->linenumber);

    while(testnext(ls,',')|| testnext(ls,TK_CASE)){
      expdesc c = clone(gv);
      expdesc v2;
      luaK_infix(ls->fs, OPR_EQ, &c);
      cond_expr(ls, &v2);  /* 使用 cond_expr 避免 { 被误解为函数调用 */
      luaK_posfix(ls->fs, OPR_EQ, &c, &v2, ls->linenumber);
      luaK_infix(ls->fs, OPR_OR, control);
      luaK_posfix(ls->fs, OPR_OR, control, &c, ls->linenumber);
    }

    leavelevel(ls);

    if(!testnext(ls, TK_DO)){
      if(!testnext(ls, TK_THEN)){
        if(!testnext(ls, ':')){
          testnext(ls, '{');
        }
      }
    }

  if (ls->t.token == TK_BREAK||ls->t.token==TK_CONTINUE) {  /* 'if x then break' ? */
    int line = ls->linenumber;
    luaK_goiffalse(ls->fs, &v);  /* will jump if condition is true */
    if(ls->t.token==TK_BREAK) {
      luaX_next(ls);  /* skip 'break' */
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      newgotoentry(ls, luaS_newliteral(ls->L, "break"), line, v.t);
    }else{
      enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
      newgotoentry(ls, luaS_newliteral(ls->L, "continue"), line, v.t);
    }
    while (testnext(ls, ';')) {}  /* skip semicolons */
    if (block_follow(ls, 0)) {  /* jump is the entire block? */
      leaveblock(fs);
      return;  /* and that is it */
    }
    else  /* must skip over 'then' part if condition is false */
      jf = luaK_jump(fs);
  }else {
    luaK_goiftrue(ls->fs, control);  /* skip over block if condition is false */
    enterblock(fs, &bl, 0);
    jf = control->f;
  }

      statlist(ls);  /* `CASE' part */
      leaveblock(fs);

      if (ls->t.token == TK_CASE ||
          ls->t.token == TK_DEFAULT)
        luaK_concat(fs, escapelist, luaK_jump(fs));
      //check_match(ls, TK_END, TK_CASE, ls->linenumber);
      luaK_patchtohere(fs, jf);
}

static void switchstat (LexState *ls, int line) {
    /* switchstat -> SWITCH var {CASE var DO block} [DEFAULT block] END */
    expdesc v, buf;
    luaX_next(ls);
    switch_read_var(ls, &v);
    if(!testnext(ls, TK_DO)){
      if(!testnext(ls, TK_THEN)){
        if (!testnext(ls, ':')){
          testnext(ls, '{');
        }
      }
    }

    FuncState *fs = ls->fs;
    int escapelist = NO_JUMP;
    while (ls->t.token == TK_CASE) {
        buf = v;
        test_case_block(ls, &escapelist, &buf);  /* CASE var DO block */
    }
    if (testnext(ls, TK_DEFAULT))
        block(ls);      /* `default' part */
    check_match(ls, TK_END, TK_SWITCH, line);
  luaK_patchtohere(fs, escapelist);
}


/*
** try-catch-finally 语句解析
** 语法: try statlist [catch(name) statlist] [finally statlist] end
** 
** 实现原理：
** 将 try-catch-finally 转换为等价的 pcall 调用：
**   local __ok__, __err__ = pcall(function() try_block end)
**   if not __ok__ then
**     local e = __err__
**     catch_block
**   end
**   finally_block
**
** 参数：
**   ls - 词法状态
**   line - try 关键字所在行号
*/
static void trystat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  int base;
  expdesc pcall_func, closure_exp, ok_var, err_var;
  int ok_reg, err_reg;
  TString *err_name = NULL;
  int has_catch = 0;
  int has_finally = 0;
  
  luaX_next(ls);  /* skip TRY */
  
  /* 进入外层 block */
  enterblock(fs, &bl, 0);
  
  /* 创建两个局部变量 __ok__ 和 __err__ */
  new_localvarliteral(ls, "__try_ok__");
  new_localvarliteral(ls, "__try_err__");
  adjustlocalvars(ls, 2);
  ok_reg = fs->nactvar - 2;
  err_reg = fs->nactvar - 1;
  
  /* 获取 pcall 全局函数 */
  singlevaraux(fs, luaS_newliteral(ls->L, "pcall"), &pcall_func, 1);
  if (pcall_func.k == VVOID) {
    expdesc key;
    singlevaraux(fs, ls->envn, &pcall_func, 1);
    codestring(&key, luaS_newliteral(ls->L, "pcall"));
    luaK_indexed(fs, &pcall_func, &key);
  }
  luaK_exp2nextreg(fs, &pcall_func);
  base = pcall_func.u.info;
  
  /* 创建闭包：function() try_block end */
  {
    FuncState new_fs;
    BlockCnt new_bl;
    new_fs.f = addprototype(ls);
    new_fs.f->linedefined = line;
    open_func(ls, &new_fs, &new_bl);
    
    /* 解析 try 块直到遇到 catch/finally/end */
    while (ls->t.token != TK_CATCH && 
           ls->t.token != TK_FINALLY && 
           ls->t.token != TK_END && 
           ls->t.token != TK_EOS) {
      statement(ls);
      if (ls->t.token == TK_RETURN) {
        statement(ls);
        break;
      }
    }
    
    new_fs.f->lastlinedefined = ls->linenumber;
    codeclosure(ls, &closure_exp);
    close_func(ls);
  }
  
  /* 将闭包放入下一个寄存器 */
  luaK_exp2nextreg(fs, &closure_exp);
  
  /* 调用 pcall(closure)，返回 ok, err */
  luaK_codeABC(fs, OP_CALL, base, 2, 3);  /* 1个参数，2个返回值 */
  fs->freereg = base + 2;
  
  /* 将结果存储到局部变量 */
  init_exp(&ok_var, VLOCAL, reglevel(fs, ok_reg));
  init_exp(&err_var, VLOCAL, reglevel(fs, err_reg));
  {
    expdesc result;
    init_exp(&result, VNONRELOC, base);
    luaK_storevar(fs, &ok_var, &result);
    init_exp(&result, VNONRELOC, base + 1);
    luaK_storevar(fs, &err_var, &result);
  }
  
  /* 解析 catch 块 */
  if (ls->t.token == TK_CATCH) {
    has_catch = 1;
    expdesc cond;
    BlockCnt catch_bl;
    int jt;  /* 跳转：ok 为真时跳过 catch 块 */
    
    luaX_next(ls);  /* skip CATCH */
    
    /* 解析 catch(e) 中的变量名 */
    checknext(ls, '(');
    err_name = str_checkname(ls);
    checknext(ls, ')');
    
    /* 生成条件跳转：如果 __ok__ 为真则跳过 catch 块 */
    init_exp(&cond, VLOCAL, reglevel(fs, ok_reg));
    luaK_exp2anyreg(fs, &cond);
    luaK_goiffalse(fs, &cond);  /* 假 -> fallthrough 执行 catch；真 -> 跳转跳过 catch */
    jt = cond.t;  /* 保存真跳转位置 */
    
    /* 进入 catch 块 */
    enterblock(fs, &catch_bl, 0);
    
    /* 创建局部变量 e = __err__ */
    new_localvar(ls, err_name);
    adjustlocalvars(ls, 1);
    {
      expdesc err_val;
      init_exp(&err_val, VLOCAL, reglevel(fs, err_reg));
      luaK_exp2nextreg(fs, &err_val);
    }
    
    /* 解析 catch 块语句 */
    while (ls->t.token != TK_FINALLY && 
           ls->t.token != TK_END && 
           ls->t.token != TK_EOS) {
      statement(ls);
      if (ls->t.token == TK_RETURN) {
        statement(ls);
        break;
      }
    }
    
    leaveblock(fs);
    luaK_patchtohere(fs, jt);  /* 真跳转跳到这里（跳过 catch） */
  }
  
  /* 解析 finally 块 */
  if (ls->t.token == TK_FINALLY) {
    has_finally = 1;
    luaX_next(ls);  /* skip FINALLY */
    
    /* finally 块无条件执行 */
    while (ls->t.token != TK_END && ls->t.token != TK_EOS) {
      statement(ls);
      if (ls->t.token == TK_RETURN) {
        statement(ls);
        break;
      }
    }
  }
  
  check_match(ls, TK_END, TK_TRY, line);
  leaveblock(fs);
}


/*
** with 语句解析
** 语法: with(expr) { block }
** 
** 实现原理：
** 将 with(expr) { block } 转换为等价代码：
**   do
**     local __with_target__ = expr
**     local __with_saved_env__ = _ENV
**     _ENV = __with_create_env__(__with_target__, __with_saved_env__)
**     -- block
**     _ENV = __with_saved_env__
**   end
**
** 参数：
**   ls - 词法状态
**   line - with 关键字所在行号
*/
static void withstat (LexState *ls, int line) {
  FuncState *fs = ls->fs;
  BlockCnt bl;
  expdesc target_exp, env_var, saved_env_exp, func_exp, new_env_exp;
  int target_reg, saved_env_reg;
  int base;
  
  luaX_next(ls);  /* skip WITH */
  
  /* 进入块作用域 */
  enterblock(fs, &bl, 0);
  
  /* 解析 with(expr) 中的表达式 */
  checknext(ls, '(');
  expr(ls, &target_exp);
  checknext(ls, ')');
  
  /* 创建局部变量 __with_target__ 存储目标表 */
  new_localvarliteral(ls, "__with_target__");
  luaK_exp2nextreg(fs, &target_exp);
  adjustlocalvars(ls, 1);
  target_reg = fs->nactvar - 1;
  
  /* 创建局部变量 __with_saved_env__ 存储原 _ENV */
  new_localvarliteral(ls, "__with_saved_env__");
  singlevaraux(fs, ls->envn, &env_var, 1);  /* 获取 _ENV */
  if (env_var.k == VVOID) {
    /* _ENV 不存在，从全局获取 */
    expdesc key;
    singlevaraux(fs, ls->envn, &env_var, 1);
  }
  luaK_exp2nextreg(fs, &env_var);
  adjustlocalvars(ls, 1);
  saved_env_reg = fs->nactvar - 1;
  
  /* 调用 __with_create_env__(__with_target__, __with_saved_env__) */
  /* 获取 __with_create_env__ 全局函数 */
  singlevaraux(fs, luaS_newliteral(ls->L, "__with_create_env__"), &func_exp, 1);
  if (func_exp.k == VVOID) {
    expdesc key;
    singlevaraux(fs, ls->envn, &func_exp, 1);
    codestring(&key, luaS_newliteral(ls->L, "__with_create_env__"));
    luaK_indexed(fs, &func_exp, &key);
  }
  luaK_exp2nextreg(fs, &func_exp);
  base = func_exp.u.info;
  
  /* 参数1: __with_target__ */
  {
    expdesc arg;
    init_exp(&arg, VLOCAL, reglevel(fs, target_reg));
    luaK_exp2nextreg(fs, &arg);
  }
  
  /* 参数2: __with_saved_env__ */
  {
    expdesc arg;
    init_exp(&arg, VLOCAL, reglevel(fs, saved_env_reg));
    luaK_exp2nextreg(fs, &arg);
  }
  
  /* 调用函数，返回新环境 */
  luaK_codeABC(fs, OP_CALL, base, 3, 2);  /* 2个参数，1个返回值 */
  fs->freereg = base + 1;
  
  /* 将新环境赋值给 _ENV */
  {
    expdesc env_dst;
    expdesc result;
    singlevaraux(fs, ls->envn, &env_dst, 1);
    init_exp(&result, VNONRELOC, base);
    luaK_storevar(fs, &env_dst, &result);
  }
  
  /* 解析块内容 */
  checknext(ls, '{');
  while (ls->t.token != '}' && ls->t.token != TK_EOS) {
    statement(ls);
  }
  checknext(ls, '}');
  
  /* 恢复 _ENV = __with_saved_env__ */
  {
    expdesc env_dst, saved_val;
    singlevaraux(fs, ls->envn, &env_dst, 1);
    init_exp(&saved_val, VLOCAL, reglevel(fs, saved_env_reg));
    luaK_exp2anyreg(fs, &saved_val);
    luaK_storevar(fs, &env_dst, &saved_val);
  }
  
  leaveblock(fs);
}

//========================================================================================


static void localfunc (LexState *ls, int isexport) {
  expdesc b;
  FuncState *fs = ls->fs;
  int fvar = fs->nactvar;  /* function's variable index */
  TString *name = str_checkname(ls);
  new_localvar(ls, name);  /* new local variable */
  if (isexport) add_export(ls, name);
  adjustlocalvars(ls, 1);  /* enter its scope */
  body(ls, &b, 0, ls->linenumber);  /* function created in next register */
  /* debug information will only see the variable after this point! */
  localdebuginfo(fs, fvar)->startpc = fs->pc;
}


static lu_byte getvarattribute (LexState *ls, lu_byte df) {
  /* attrib -> ['<' NAME '>'] */
  if (testnext(ls, '<')) {
    const char *attr;
    if (ls->t.token == TK_CONST) {
      attr = "const";
      luaX_next(ls);
    }
    else
      attr = getstr(str_checkname(ls));
    checknext(ls, '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  /* read-only variable */
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  /* to-be-closed variable */
    else
      luaK_semerror(ls, "unknown attribute '%s'", attr);
  }
  return df;  /* return default value */
}


static void checktoclose (FuncState *fs, int level) {
  if (level != -1) {  /* is there a to-be-closed variable? */
    marktobeclosed(fs);
    luaK_codeABC(fs, OP_TBC, reglevel(fs, level), 0, 0);
  }
}


/*
** =======================================================================
** take 解构语法实现
** 语法格式: local take {变量列表} = 目标表
** 支持: 基础键值解构、缺省值、数组解构(跳过元素)、嵌套解构
** =======================================================================
*/

/* 解构项的最大数量 */
#define MAX_DESTRUCT_ITEMS 64

/* 解构项类型 */
typedef struct DestructItem {
  TString *varname;       /* 局部变量名 */
  TString *keyname;       /* 表中的键名 (如果为NULL则使用varname) */
  int array_index;        /* 数组索引 (0表示键值模式, >0表示数组模式) */
  int has_default;        /* 是否有默认值 */
  int default_reg;        /* 默认值所在寄存器 */
  int is_nested;          /* 是否是嵌套解构 */
  int nested_start;       /* 嵌套解构的起始索引 */
  int nested_count;       /* 嵌套解构的项数量 */
} DestructItem;

/*
** 解析解构项列表
** 参数:
**   ls: 词法状态
**   items: 解构项数组
**   max_items: 最大项数
**   array_mode: 是否为数组模式 (检测到跳过元素时自动切换)
** 返回: 解构项数量
*/
static int parse_destruct_items(LexState *ls, DestructItem *items, int max_items, int *array_mode) {
  int count = 0;
  int array_idx = 1;  /* 数组索引从1开始 */
  
  checknext(ls, '{');
  
  while (ls->t.token != '}' && count < max_items) {
    DestructItem *item = &items[count];
    memset(item, 0, sizeof(DestructItem));
    
    /* 检测跳过元素 (空位，如 {a, , b}) */
    if (ls->t.token == ',') {
      /* 切换到数组模式 */
      *array_mode = 1;
      array_idx++;  /* 跳过这个索引 */
      luaX_next(ls);  /* 跳过逗号 */
      continue;
    }
    
    /* 检测是否是嵌套解构: name = {nested} 或直接 {nested} */
    if (ls->t.token == '{') {
      /* 直接嵌套解构，不支持这种形式，报错 */
      luaX_syntaxerror(ls, "嵌套解构必须指定键名，如: addr = {city}");
    }
    
    /* 解析变量名/键名 */
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "解构项需要标识符");
    }
    item->varname = ls->t.seminfo.ts;
    item->keyname = item->varname;  /* 默认键名与变量名相同 */
    luaX_next(ls);
    
    /* 检测默认值或嵌套解构: name = expr 或 name = {nested} */
    if (testnext(ls, '=')) {
      if (ls->t.token == '{') {
        /* 嵌套解构: name = {nested_items} */
        item->is_nested = 1;
        item->nested_start = count + 1;
        
        /* 递归解析嵌套项 */
        int nested_array_mode = 0;
        int nested_count = parse_destruct_items(ls, &items[count + 1], 
                                                 max_items - count - 1, 
                                                 &nested_array_mode);
        item->nested_count = nested_count;
        count += nested_count;  /* 跳过嵌套项 */
      } else {
        /* 有默认值 */
        item->has_default = 1;
        /* 默认值表达式稍后在代码生成阶段处理 */
      }
    }
    
    /* 设置数组索引 */
    if (*array_mode) {
      item->array_index = array_idx++;
    }
    
    count++;
    
    /* 检查逗号或结束 */
    if (ls->t.token == ',') {
      luaX_next(ls);
      /* 如果是数组模式，增加索引 */
      if (*array_mode && ls->t.token != '}') {
        /* 索引将在下一次循环开始时设置 */
      }
    } else if (ls->t.token != '}') {
      luaX_syntaxerror(ls, "解构列表中期望 ',' 或 '}'");
    }
  }
  
  checknext(ls, '}');
  return count;
}

/*
** 为单个解构项生成代码
** 参数:
**   ls: 词法状态
**   item: 解构项
**   source_reg: 源表寄存器
**   items: 所有解构项 (用于嵌套解构)
**   all_count: 所有解构项数量
*/
static void codegen_destruct_item(LexState *ls, DestructItem *item, int source_reg,
                                   DestructItem *items, int all_count) {
  FuncState *fs = ls->fs;
  expdesc source, key, val;
  
  if (item->is_nested) {
    /* 嵌套解构: 先获取嵌套表，然后递归处理 */
    expdesc nested_table;
    int nested_reg;
    
    /* 获取嵌套表: source[keyname] */
    init_exp(&source, VNONRELOC, source_reg);
    codestring(&key, item->keyname);
    luaK_indexed(fs, &source, &key);
    luaK_exp2nextreg(fs, &source);
    nested_reg = source.u.info;
    
    /* 递归处理嵌套项 */
    int i;
    for (i = 0; i < item->nested_count; i++) {
      DestructItem *nested_item = &items[item->nested_start + i];
      if (!nested_item->is_nested) {
        codegen_destruct_item(ls, nested_item, nested_reg, items, all_count);
      }
    }
    
    fs->freereg = nested_reg;  /* 释放嵌套表寄存器 */
    return;
  }
  
  /* 创建局部变量 */
  int vidx = new_localvar(ls, item->varname);
  
  /* 生成从源表读取值的代码 */
  init_exp(&source, VNONRELOC, source_reg);
  
  if (item->array_index > 0) {
    /* 数组模式: source[index] */
    init_exp(&key, VKINT, 0);
    key.u.ival = item->array_index;
  } else {
    /* 键值模式: source[keyname] */
    codestring(&key, item->keyname);
  }
  
  luaK_indexed(fs, &source, &key);
  luaK_exp2nextreg(fs, &source);
  
  /* 调整局部变量 */
  adjustlocalvars(ls, 1);
}



/*
** takestat_full - 解析 take 解构语句
** 语法: local take {name, age, ...} = source_table
** 
** 将表字段解构到局部变量
** 支持: 键值解构、数组解构(跳过元素)、嵌套解构
**
** 参数:
**   ls - 词法分析器状态
*/
static void takestat_full(LexState *ls) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc source_exp;
  int source_reg;
  
  /* 收集变量信息 */
  TString *varnames[MAX_DESTRUCT_ITEMS];
  TString *keynames[MAX_DESTRUCT_ITEMS];
  int array_indices[MAX_DESTRUCT_ITEMS];
  int is_nested[MAX_DESTRUCT_ITEMS];
  TString *nested_keyname[MAX_DESTRUCT_ITEMS];
  int nvars = 0;
  int array_mode = 0;
  int array_idx = 1;
  int i;
  
  checknext(ls, '{');
  
  /* 第一阶段：收集所有变量信息 */
  while (ls->t.token != '}' && nvars < MAX_DESTRUCT_ITEMS) {
    /* 跳过空位（数组模式） */
    if (ls->t.token == ',') {
      array_mode = 1;
      array_idx++;
      luaX_next(ls);
      continue;
    }
    
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "解构项需要标识符");
    }
    
    TString *name = ls->t.seminfo.ts;
    luaX_next(ls);
    
    varnames[nvars] = name;
    keynames[nvars] = name;
    array_indices[nvars] = array_mode ? array_idx : 0;
    is_nested[nvars] = 0;
    nested_keyname[nvars] = NULL;
    
    if (testnext(ls, '=')) {
      if (ls->t.token == '{') {
        /* 嵌套解构: name = {fields} */
        TString *parent_key = name;
        luaX_next(ls);  /* 跳过 '{' */
        
        /* 不为父项创建变量，只为嵌套项创建 */
        nvars--;  /* 撤销父项 */
        
        while (ls->t.token != '}' && nvars < MAX_DESTRUCT_ITEMS) {
          if (ls->t.token == ',') {
            luaX_next(ls);
            continue;
          }
          
          if (ls->t.token != TK_NAME) {
            luaX_syntaxerror(ls, "嵌套解构项需要标识符");
          }
          
          varnames[nvars] = ls->t.seminfo.ts;
          keynames[nvars] = varnames[nvars];
          array_indices[nvars] = 0;
          is_nested[nvars] = 1;
          nested_keyname[nvars] = parent_key;
          
          luaX_next(ls);
          
          /* 跳过默认值（暂不支持） */
          if (testnext(ls, '=')) {
            int depth = 0;
            while (ls->t.token != ',' && ls->t.token != '}' && ls->t.token != TK_EOS) {
              if (ls->t.token == '{') depth++;
              else if (ls->t.token == '}' && depth > 0) depth--;
              else if (ls->t.token == '}' && depth == 0) break;
              luaX_next(ls);
            }
          }
          
          nvars++;
          
          if (ls->t.token == ',') {
            luaX_next(ls);
          }
        }
        checknext(ls, '}');
        
        if (array_mode) array_idx++;
        if (ls->t.token == ',') luaX_next(ls);
        continue;
      } else {
        /* 跳过默认值（暂不支持） */
        int depth = 0;
        while (ls->t.token != ',' && ls->t.token != '}' && ls->t.token != TK_EOS) {
          if (ls->t.token == '(' || ls->t.token == '{' || ls->t.token == '[') depth++;
          else if ((ls->t.token == ')' || ls->t.token == '}' || ls->t.token == ']') && depth > 0) depth--;
          else if (ls->t.token == '}' && depth == 0) break;
          luaX_next(ls);
        }
      }
    }
    
    if (array_mode) {
      array_idx++;
    }
    
    nvars++;
    
    if (ls->t.token == ',') {
      luaX_next(ls);
    }
  }
  
  checknext(ls, '}');
  checknext(ls, '=');
  
  /* 第二阶段：创建所有局部变量 */
  for (i = 0; i < nvars; i++) {
    new_localvar(ls, varnames[i]);
  }
  
  /* 获取变量起始寄存器 */
  int var_base = luaY_nvarstack(fs);
  
  /* 预留寄存器空间给变量 */
  luaK_reserveregs(fs, nvars);
  
  /* 第三阶段：解析源表达式到临时寄存器（在变量区域之后） */
  expr(ls, &source_exp);
  luaK_exp2nextreg(fs, &source_exp);
  source_reg = source_exp.u.info;
  
  /* 第四阶段：为每个变量生成从源表读取值的代码 */
  for (i = 0; i < nvars; i++) {
    expdesc src, key_exp;
    int target_reg = var_base + i;
    int actual_source = source_reg;
    
    /* 如果是嵌套项，先获取嵌套表 */
    if (is_nested[i] && nested_keyname[i] != NULL) {
      expdesc nested_src, nested_key;
      init_exp(&nested_src, VNONRELOC, source_reg);
      codestring(&nested_key, nested_keyname[i]);
      luaK_indexed(fs, &nested_src, &nested_key);
      luaK_exp2nextreg(fs, &nested_src);
      actual_source = nested_src.u.info;
    }
    
    /* 生成 table[key] 的读取代码 */
    init_exp(&src, VNONRELOC, actual_source);
    if (array_indices[i] > 0) {
      /* 数组模式: table[index] */
      init_exp(&key_exp, VKINT, 0);
      key_exp.u.ival = array_indices[i];
    } else {
      /* 键值模式: table[keyname] */
      codestring(&key_exp, keynames[i]);
    }
    luaK_indexed(fs, &src, &key_exp);
    
    /* 将值直接加载到目标寄存器 */
    luaK_exp2reg(fs, &src, target_reg);
    
    /* 重置 freereg 保护源表（在 source_reg 之后） */
    fs->freereg = source_reg + 1;
  }
  
  /* 释放源表临时寄存器，freereg 回到变量区域末尾 */
  fs->freereg = var_base + nvars;
  
  /* 第五阶段：激活所有变量 */
  adjustlocalvars(ls, nvars);
}

static void localstat (LexState *ls, int isexport) {
  /* stat -> LOCAL ATTRIB NAME ATTRIB { ',' NAME ATTRIB } ['=' explist] */
  /* stat -> CONST NAME ATTRIB { ',' NAME ATTRIB } ['=' explist] */
  FuncState *fs = ls->fs;
  int base_nactvar = fs->nactvar;
  Vardesc *var;  /* last variable */
  int vidx, kind;
  int nvars = 0;
  int nexps;
  expdesc e;
  /* check if this is a const declaration */
  int isconst = (ls->lasttoken == TK_CONST);
  lu_byte defkind = getvarattribute(ls, isconst ? RDKCONST : VDKREG);
  
  do {
    TString *varname = str_checkname(ls);
    /* 检查变量是否已经存在 */
    if (isconst) {
      /* 对于const声明，检查变量是否已经存在于当前作用域 */
      int i;
      for (i = cast_int(fs->nactvar) - 1; i >= 0; i--) {
        Vardesc *vd = getlocalvardesc(fs, i);
        if (eqstr(varname, vd->vd.name)) {  /* 找到同名变量 */
          /* 检查是否是const变量 */
          if (vd->vd.kind == RDKCONST || vd->vd.kind == RDKCTC) {
            luaK_semerror(ls, "const variable '%s' already defined", getstr(varname));
          }
          /* 非const变量允许被const重新声明，跳出循环 */
          break;
        }
      }
    }
    vidx = new_localvar(ls, varname);
    if (isexport) {
        add_export(ls, varname);
    }
    kind = getvarattribute(ls, defkind);
    getlocalvardesc(fs, vidx)->vd.kind = kind;
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))
    nexps = explist(ls, &e);
  else {
    e.k = VVOID;
    nexps = 0;
    /* const variables must be initialized */
    if (isconst)
      luaK_semerror(ls, "const variable must be initialized");
  }
  var = getlocalvardesc(fs, vidx);  /* get last variable */
  if (nvars == nexps &&  /* no adjustments? */
      var->vd.kind == RDKCONST &&  /* last variable is const? */
      luaK_exp2const(fs, &e, &var->k)) {  /* compile-time constant? */
    var->vd.kind = RDKCTC;  /* variable is a compile-time constant */
    adjustlocalvars(ls, nvars - 1);  /* exclude last variable */
    fs->nactvar++;  /* but count it */
  }
  else {
    adjust_assign(ls, nvars, nexps, &e);
    adjustlocalvars(ls, nvars);
  }
  /* handle to-be-closed variables */
  for (int i = 0; i < nvars; i++) {
    int idx = base_nactvar + i;
    Vardesc *vd = getlocalvardesc(fs, idx);
    if (vd->vd.kind == RDKTOCLOSE) {
      checktoclose(fs, idx);
    }
  }
}


static lu_byte getglobalattribute (LexState *ls, lu_byte df) {
  lu_byte kind = getvarattribute(ls, df);
  switch (kind) {
    case RDKTOCLOSE:
      luaK_semerror(ls, "global variables cannot be to-be-closed");
      return kind;  /* to avoid warnings */
    case RDKCONST:
      return GDKCONST;  /* adjust kind for global variable */
    default:
      return kind;
  }
}


static void checkglobal (LexState *ls, TString *varname, int line) {
  FuncState *fs = ls->fs;
  expdesc var;
  int k;
  buildglobal(ls, varname, &var);  /* create global variable in 'var' */
  k = var.u.ind.keystr;  /* index of global name in 'k' */
  luaK_codecheckglobal(fs, &var, k, line);
}


/*
** Recursively traverse list of globals to be initalized. When
** going, generate table description for the global. In the end,
** after all indices have been generated, read list of initializing
** expressions. When returning, generate the assignment of the value on
** the stack to the corresponding table description. 'n' is the variable
** being handled, range [0, nvars - 1].
*/
static void initglobal (LexState *ls, int nvars, int firstidx, int n,
                        int line) {
  if (n == nvars) {  /* traversed all variables? */
    expdesc e;
    int nexps = explist(ls, &e);  /* read list of expressions */
    adjust_assign(ls, nvars, nexps, &e);
  }
  else {  /* handle variable 'n' */
    FuncState *fs = ls->fs;
    expdesc var;
    TString *varname = getlocalvardesc(fs, firstidx + n)->vd.name;
    buildglobal(ls, varname, &var);  /* create global variable in 'var' */
    enterlevel(ls);  /* control recursion depth */
    initglobal(ls, nvars, firstidx, n + 1, line);
    leavelevel(ls);
    checkglobal(ls, varname, line);
    storevartop(fs, &var);
  }
}


static void globalnames (LexState *ls, lu_byte defkind) {
  FuncState *fs = ls->fs;
  int nvars = 0;
  int lastidx;  /* index of last registered variable */
  do {  /* for each name */
    TString *vname = str_checkname(ls);
    lu_byte kind = getglobalattribute(ls, defkind);
    lastidx = new_varkind(ls, vname, kind);
    nvars++;
  } while (testnext(ls, ','));
  if (testnext(ls, '='))  /* initialization? */
    initglobal(ls, nvars, lastidx - nvars + 1, 0, ls->linenumber);
  fs->nactvar = cast_short(fs->nactvar + nvars);  /* activate declaration */
}


static void globalstat (LexState *ls) {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *fs = ls->fs;
  /* get prefixed attribute (if any); default is regular global variable */
  lu_byte defkind = getglobalattribute(ls, GDKREG);
  if (!testnext(ls, '*'))
    globalnames(ls, defkind);
  else {
    /* use NULL as name to represent '*' entries */
    new_varkind(ls, NULL, defkind);
    fs->nactvar++;  /* activate declaration */
  }
}


static void globalfunc (LexState *ls, int line) {
  /* globalfunc -> (GLOBAL FUNCTION) NAME body */
  expdesc var, b;
  FuncState *fs = ls->fs;
  TString *fname = str_checkname(ls);
  new_varkind(ls, fname, GDKREG);  /* declare global variable */
  fs->nactvar++;  /* enter its scope */
  buildglobal(ls, fname, &var);
  body(ls, &b, 0, ls->linenumber);  /* compile and return closure in 'b' */
  checkglobal(ls, fname, line);
  luaK_storevar(fs, &var, &b);
  luaK_fixline(fs, line);  /* definition "happens" in the first line */
}


static void globalstatfunc (LexState *ls, int line) {
  /* stat -> GLOBAL globalfunc | GLOBAL globalstat */
  luaX_next(ls);  /* skip 'global' */
  if (testnext(ls, TK_FUNCTION))
    globalfunc(ls, line);
  else
    globalstat(ls);
}


static int funcname (LexState *ls, expdesc *v) {
  /* funcname -> NAME {fieldsel} [':' NAME] */
  int ismethod = 0;
  singlevar(ls, v);
  while (ls->t.token == '.')
    fieldsel(ls, v);
  if (ls->t.token == ':') {
    ismethod = 1;
    fieldsel(ls, v);
  }
  return ismethod;
}


/*
** ============================================================
** 内联汇编支持 (asm statement)
** ============================================================
*/

/*
** 汇编标签结构（用于跳转目标）
** 支持前向引用和后向引用
*/
#define ASM_INIT_LABELS 16    /* 标签初始容量 */
#define ASM_INIT_PENDING 32   /* 待修补跳转初始容量 */
#define ASM_INIT_DEFINES 16   /* 汇编常量定义初始容量 */

typedef struct AsmLabel {
  TString *name;    /* 标签名称 */
  int pc;           /* 标签对应的 PC 位置，-1 表示尚未定义 */
  int line;         /* 标签定义的行号 */
} AsmLabel;

typedef struct AsmPending {
  TString *label;   /* 目标标签名称 */
  int pc;           /* 需要修补的指令位置 */
  int line;         /* 引用标签的行号 */
  int isJump;       /* 是否是跳转指令（需要计算偏移） */
} AsmPending;

/*
** 汇编常量定义结构
** 用于 def 伪指令定义的编译期常量
*/
typedef struct AsmDefine {
  TString *name;    /* 常量名称 */
  lua_Integer value;/* 常量值 */
} AsmDefine;

typedef struct AsmContext {
  AsmLabel *labels;       /* 标签动态数组 */
  int nlabels;            /* 标签数量 */
  int labels_cap;         /* 标签数组容量 */
  AsmPending *pending;    /* 待修补跳转动态数组 */
  int npending;           /* 待修补数量 */
  int pending_cap;        /* 待修补数组容量 */
  AsmDefine *defines;     /* 常量定义动态数组 */
  int ndefines;           /* 常量定义数量 */
  int defines_cap;        /* 常量定义数组容量 */
  struct AsmContext *parent;  /* 父级上下文（用于嵌套 asm） */
} AsmContext;


/*
** 初始化汇编上下文
** 参数：
**   L - Lua 状态机（用于内存分配）
**   ctx - 汇编上下文
**   parent - 父级上下文（嵌套时非 NULL）
*/
static void asm_initcontext (lua_State *L, AsmContext *ctx, AsmContext *parent) {
  ctx->labels = luaM_newvector(L, ASM_INIT_LABELS, AsmLabel);
  ctx->nlabels = 0;
  ctx->labels_cap = ASM_INIT_LABELS;
  ctx->pending = luaM_newvector(L, ASM_INIT_PENDING, AsmPending);
  ctx->npending = 0;
  ctx->pending_cap = ASM_INIT_PENDING;
  ctx->defines = luaM_newvector(L, ASM_INIT_DEFINES, AsmDefine);
  ctx->ndefines = 0;
  ctx->defines_cap = ASM_INIT_DEFINES;
  ctx->parent = parent;
}


/*
** 释放汇编上下文
** 参数：
**   L - Lua 状态机
**   ctx - 汇编上下文
*/
static void asm_freecontext (lua_State *L, AsmContext *ctx) {
  luaM_freearray(L, ctx->labels, ctx->labels_cap);
  luaM_freearray(L, ctx->pending, ctx->pending_cap);
  luaM_freearray(L, ctx->defines, ctx->defines_cap);
  ctx->labels = NULL;
  ctx->pending = NULL;
  ctx->defines = NULL;
  ctx->nlabels = ctx->npending = ctx->ndefines = 0;
  ctx->labels_cap = ctx->pending_cap = ctx->defines_cap = 0;
}


/*
** 根据操作码名称查找对应的 OpCode
** 参数：
**   name - 操作码名称字符串
** 返回值：
**   对应的 OpCode，如果找不到则返回 -1
*/
static int find_opcode (const char *name) {
  int i;
  for (i = 0; opnames[i] != NULL; i++) {
    if (strcmp(opnames[i], name) == 0)
      return i;
  }
  return -1;
}


/*
** 在汇编上下文中查找标签
** 参数：
**   ctx - 汇编上下文
**   name - 标签名称
** 返回值：
**   标签索引，如果找不到则返回 -1
*/
static int asm_findlabel (AsmContext *ctx, TString *name) {
  int i;
  for (i = 0; i < ctx->nlabels; i++) {
    if (ctx->labels[i].name == name)
      return i;
  }
  return -1;
}


/*
** 定义汇编标签
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 标签名称
**   pc - 标签位置
**   line - 行号
*/
static void asm_deflabel (LexState *ls, AsmContext *ctx, TString *name, int pc, int line) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0) {
    /* 标签已存在 */
    if (ctx->labels[idx].pc >= 0) {
      luaK_semerror(ls, "duplicate label '%s' in asm", getstr(name));
    }
    /* 标签之前被前向引用，现在定义它 */
    ctx->labels[idx].pc = pc;
    ctx->labels[idx].line = line;
  }
  else {
    /* 新标签 - 动态扩容 */
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = pc;
    ctx->labels[ctx->nlabels].line = line;
    ctx->nlabels++;
  }
}


/*
** 在汇编上下文中查找常量定义（包括父级上下文）
** 参数：
**   ctx - 汇编上下文
**   name - 常量名称
**   out_ctx - 输出参数，返回找到定义的上下文（可为 NULL）
** 返回值：
**   常量索引，如果找不到则返回 -1
*/
static int asm_finddefine_ex (AsmContext *ctx, TString *name, AsmContext **out_ctx) {
  AsmContext *cur = ctx;
  while (cur != NULL) {
    int i;
    for (i = 0; i < cur->ndefines; i++) {
      if (cur->defines[i].name == name) {
        if (out_ctx) *out_ctx = cur;
        return i;
      }
    }
    cur = cur->parent;  /* 向上查找父级上下文 */
  }
  if (out_ctx) *out_ctx = NULL;
  return -1;
}


/*
** 在汇编上下文中查找常量定义
** 参数：
**   ctx - 汇编上下文
**   name - 常量名称
** 返回值：
**   常量索引，如果找不到则返回 -1
*/
static int asm_finddefine (AsmContext *ctx, TString *name) {
  return asm_finddefine_ex(ctx, name, NULL);
}


/*
** 添加或更新汇编常量定义
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 常量名称
**   value - 常量值
*/
static void asm_adddefine (LexState *ls, AsmContext *ctx, TString *name, lua_Integer value) {
  int idx;
  int i;
  /* 只在当前上下文中查找（不向上查找父级） */
  for (i = 0; i < ctx->ndefines; i++) {
    if (ctx->defines[i].name == name) {
      /* 更新已存在的定义 */
      ctx->defines[i].value = value;
      return;
    }
  }
  /* 新定义 - 动态扩容 */
  if (ctx->ndefines >= ctx->defines_cap) {
    int newcap = ctx->defines_cap * 2;
    ctx->defines = luaM_reallocvector(ls->L, ctx->defines, ctx->defines_cap, newcap, AsmDefine);
    ctx->defines_cap = newcap;
  }
  ctx->defines[ctx->ndefines].name = name;
  ctx->defines[ctx->ndefines].value = value;
  ctx->ndefines++;
  (void)idx;  /* unused */
}


/*
** 引用汇编标签（可能是前向引用）
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   name - 标签名称
** 返回值：
**   标签的 PC 位置，如果是前向引用则返回 -1
*/
static int asm_reflabel (LexState *ls, AsmContext *ctx, TString *name) {
  int idx = asm_findlabel(ctx, name);
  if (idx >= 0 && ctx->labels[idx].pc >= 0) {
    return ctx->labels[idx].pc;
  }
  /* 前向引用或未定义，先创建占位标签 */
  if (idx < 0) {
    /* 动态扩容 */
    if (ctx->nlabels >= ctx->labels_cap) {
      int newcap = ctx->labels_cap * 2;
      ctx->labels = luaM_reallocvector(ls->L, ctx->labels, ctx->labels_cap, newcap, AsmLabel);
      ctx->labels_cap = newcap;
    }
    ctx->labels[ctx->nlabels].name = name;
    ctx->labels[ctx->nlabels].pc = -1;  /* 尚未定义 */
    ctx->labels[ctx->nlabels].line = ls->linenumber;
    ctx->nlabels++;
  }
  return -1;  /* 返回 -1 表示需要后续修补 */
}


/*
** 添加待修补的跳转指令
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文
**   label - 目标标签名称
**   pc - 指令位置
**   line - 行号
**   isJump - 是否需要计算相对偏移
*/
static void asm_addpending (LexState *ls, AsmContext *ctx, TString *label, 
                            int pc, int line, int isJump) {
  /* 动态扩容 */
  if (ctx->npending >= ctx->pending_cap) {
    int newcap = ctx->pending_cap * 2;
    ctx->pending = luaM_reallocvector(ls->L, ctx->pending, ctx->pending_cap, newcap, AsmPending);
    ctx->pending_cap = newcap;
  }
  ctx->pending[ctx->npending].label = label;
  ctx->pending[ctx->npending].pc = pc;
  ctx->pending[ctx->npending].line = line;
  ctx->pending[ctx->npending].isJump = isJump;
  ctx->npending++;
}


/*
** 修补所有待处理的跳转指令
** 参数：
**   ls - 词法状态
**   fs - 函数状态
**   ctx - 汇编上下文
*/
static void asm_patchpending (LexState *ls, FuncState *fs, AsmContext *ctx) {
  int i;
  for (i = 0; i < ctx->npending; i++) {
    AsmPending *p = &ctx->pending[i];
    int idx = asm_findlabel(ctx, p->label);
    if (idx < 0 || ctx->labels[idx].pc < 0) {
      luaK_semerror(ls, "undefined label '%s' in asm", getstr(p->label));
    }
    int target = ctx->labels[idx].pc;
    Instruction *inst = &fs->f->code[p->pc];
    OpCode op = GET_OPCODE(*inst);
    
    if (p->isJump) {
      /* 计算相对偏移并修补跳转指令 */
      int offset = target - (p->pc + 1);  /* 相对于下一条指令的偏移 */
      if (getOpMode(op) == isJ) {
        /* isJ 格式：sJ 参数 */
        SETARG_sJ(*inst, offset);
      }
      else if (getOpMode(op) == iAsBx) {
        /* iAsBx 格式：sBx 参数 */
        SETARG_sBx(*inst, offset);
      }
      else {
        /* 其他格式：检查是否是 loop 相关指令 (iABx 格式) */
        if (op == OP_FORLOOP || op == OP_TFORLOOP) {
          /* FORLOOP/TFORLOOP 使用无符号 Bx 表示向后跳转偏移: pc -= Bx */
          /* offset = target - (pc + 1) */
          /* 所以 Bx = -offset = (pc + 1) - target */
          if (offset > 0) {
            luaK_semerror(ls, "jump target for loop instruction must be backward");
          }
          offset = -offset;
          if (offset > MAXARG_Bx) {
            luaK_semerror(ls, "control structure too long");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else if (op == OP_FORPREP || op == OP_TFORPREP) {
          /* FORPREP/TFORPREP 使用无符号 Bx 表示向前跳转偏移: pc += Bx (+1 for FORPREP) */
          /* 注意：FORPREP 在 lvm.c 中 pc += Bx + 1，所以 Bx = offset - 1 */
          /* TFORPREP 在 lvm.c 中 pc += Bx，所以 Bx = offset */

          if (offset < 0) {
            luaK_semerror(ls, "jump target for prep instruction must be forward");
          }

          if (op == OP_FORPREP) offset--;

          if (offset < 0 || offset > MAXARG_Bx) {
             luaK_semerror(ls, "control structure too long or invalid target");
          }
          SETARG_Bx(*inst, cast_uint(offset));
        }
        else {
          /* 其他情况：直接设置为目标 PC */
          SETARG_Bx(*inst, cast_uint(target));
        }
      }
    }
    else {
      /* 非跳转指令，直接使用目标 PC */
      /* 根据指令格式设置适当的参数 */
      enum OpMode mode = getOpMode(op);
      if (mode == iABx || mode == iAsBx) {
        SETARG_Bx(*inst, cast_uint(target));
      }
      else if (mode == iAx) {
        SETARG_Ax(*inst, target);
      }
      else {
        /* iABC 格式，假设目标在 B 或 C 字段 */
        SETARG_B(*inst, target);
      }
    }
  }
}


/*
** 检查参数是否在有效范围内
** 参数：
**   ls - 词法状态
**   val - 参数值
**   max - 最大值
**   name - 参数名称（用于错误消息）
*/
static void asm_checkrange (LexState *ls, lua_Integer val, lua_Integer max, const char *name) {
  if (val < 0 || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, max %lld)", 
                  name, (long long)val, (long long)max);
  }
}


/*
** 检查带符号参数是否在有效范围内
** 参数：
**   ls - 词法状态
**   val - 参数值
**   min - 最小值
**   max - 最大值
**   name - 参数名称
*/
static void asm_checkrange_signed (LexState *ls, lua_Integer val, 
                                   lua_Integer min, lua_Integer max, const char *name) {
  if (val < min || val > max) {
    luaK_semerror(ls, "asm %s out of range (got %lld, range %lld to %lld)", 
                  name, (long long)val, (long long)min, (long long)max);
  }
}


/*
** 解析汇编指令中的整数参数
** 支持以下格式：
**   123       - 整数字面量
**   -123      - 负整数字面量
**   $varname  - 局部变量的寄存器编号
**   ^varname  - upvalue 的索引
**   #"str"    - 字符串常量的常量池索引
**   #123      - 整数值（直接返回，用于 LOADI）
**   #3.14     - 浮点数值（截断为整数，用于 LOADF）
**   #K123     - 将整数添加到常量池并返回索引
**   #KF3.14   - 将浮点数添加到常量池并返回索引
**   @         - 当前 PC 位置
**   @label    - 标签的 PC 位置（支持前向引用）
**   !freereg  - 当前空闲寄存器编号
**   !nactvar  - 当前活跃局部变量数量
**   !pc       - 当前 PC（与 @ 相同）
** 参数：
**   ls - 词法状态
**   ctx - 汇编上下文（用于标签引用，可为 NULL）
**   pendingPc - 如果遇到前向标签引用，记录需要修补的PC位置（输出参数）
**   pendingLabel - 如果遇到前向标签引用，记录标签名称（输出参数）
** 返回值：
**   解析到的整数值
*/
static lua_Integer asm_getint_ex (LexState *ls, AsmContext *ctx, 
                                   int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  lua_Integer val;
  FuncState *fs = ls->fs;
  
  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;
  
  if (ls->t.token == TK_INT) {
    val = ls->t.seminfo.i;
    luaX_next(ls);
    return val;
  }
  else if (ls->t.token == '-') {
    luaX_next(ls);
    check(ls, TK_INT);
    val = -ls->t.seminfo.i;
    luaX_next(ls);
    return val;
  }
  else if (ls->t.token == TK_DOLLAR) {
    /* $varname - 获取局部变量的寄存器编号 */
    /* 注意: '$' 在词法分析器中被映射为 TK_DOLLAR */
    expdesc var;
    TString *varname;
    int varkind;
    luaX_next(ls);  /* 跳过 '$' */
    check(ls, TK_NAME);
    varname = ls->t.seminfo.ts;
    varkind = searchvar(fs, varname, &var);
    if (varkind < 0) {
      luaK_semerror(ls, "undefined local variable '%s' in asm", getstr(varname));
    }
    luaX_next(ls);  /* 跳过变量名 */
    return var.u.var.ridx;  /* 返回寄存器索引 */
  }
  else if (ls->t.token == '%') {
    /* %n - 直接使用寄存器编号（无需预先声明变量） */
    luaX_next(ls);  /* 跳过 '%' */
    check(ls, TK_INT);
    val = ls->t.seminfo.i;
    if (val < 0 || val > 255) {
      luaK_semerror(ls, "register index out of range (0-255) in asm: %lld", (long long)val);
    }
    luaX_next(ls);
    return val;
  }
  else if (ls->t.token == TK_NAME) {
    /* 检查是否是 Rn/rn 格式的寄存器引用 */
    const char *name = getstr(ls->t.seminfo.ts);
    TString *ts = ls->t.seminfo.ts;
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      /* 解析寄存器编号 */
      val = 0;
      int i = 1;
      while (name[i] >= '0' && name[i] <= '9') {
        val = val * 10 + (name[i] - '0');
        i++;
      }
      if (name[i] == '\0') {  /* 确保格式正确，如 R0, R10, R255 */
        if (val > 255) {
          luaK_semerror(ls, "register index out of range (0-255) in asm: R%lld", (long long)val);
        }
        luaX_next(ls);
        return val;
      }
    }
    /* 检查是否是通过 def 定义的常量（包括父级上下文） */
    if (ctx != NULL) {
      AsmContext *found_ctx = NULL;
      int defIdx = asm_finddefine_ex(ctx, ts, &found_ctx);
      if (defIdx >= 0 && found_ctx != NULL) {
        luaX_next(ls);
        return found_ctx->defines[defIdx].value;
      }
    }
    /* 不是寄存器格式也不是定义的常量，报错 */
    luaX_syntaxerror(ls, "integer expected in asm instruction");
    return 0;
  }
  else if (ls->t.token == '^') {
    /* ^varname - 获取 upvalue 的索引 */
    TString *varname;
    int idx;
    luaX_next(ls);  /* 跳过 '^' */
    check(ls, TK_NAME);
    varname = ls->t.seminfo.ts;
    idx = searchupvalue(fs, varname);
    if (idx < 0) {
      luaK_semerror(ls, "undefined upvalue '%s' in asm", getstr(varname));
    }
    luaX_next(ls);  /* 跳过变量名 */
    return idx;
  }
  else if (ls->t.token == '#') {
    /* #constant - 常量相关操作 */
    luaX_next(ls);  /* 跳过 '#' */
    if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
      /* 字符串常量 - 添加到常量池并返回索引 */
      TString *s = ls->t.seminfo.ts;
      val = luaK_stringK(fs, s);
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_INT) {
      /* #123 - 整数值本身（用于 LOADI） */
      val = ls->t.seminfo.i;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == TK_FLT) {
      /* #3.14 - 浮点数值（截断为整数，用于 LOADF） */
      val = (lua_Integer)ls->t.seminfo.r;
      luaX_next(ls);
      return val;
    }
    else if (ls->t.token == '-') {
      /* 负数常量 */
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        val = -ls->t.seminfo.i;
        luaX_next(ls);
        return val;
      }
      else if (ls->t.token == TK_FLT) {
        val = (lua_Integer)(-ls->t.seminfo.r);
        luaX_next(ls);
        return val;
      }
      else {
        luaX_syntaxerror(ls, "number expected after '#-' in asm");
        return 0;
      }
    }
    else if (ls->t.token == TK_NAME) {
      /* #K... - 添加到常量池 */
      const char *name = getstr(ls->t.seminfo.ts);
      if (name[0] == 'K' || name[0] == 'k') {
        if (name[1] == 'F' || name[1] == 'f') {
          /* #KF... - 将浮点数添加到常量池 */
          luaX_next(ls);
          if (ls->t.token == TK_FLT) {
            val = luaK_numberK(fs, ls->t.seminfo.r);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == TK_INT) {
            val = luaK_numberK(fs, (lua_Number)ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_FLT) {
              val = luaK_numberK(fs, -ls->t.seminfo.r);
              luaX_next(ls);
              return val;
            }
            else if (ls->t.token == TK_INT) {
              val = luaK_numberK(fs, (lua_Number)(-ls->t.seminfo.i));
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "number expected after '#KF' in asm");
          return 0;
        }
        else if (name[1] == 'I' || name[1] == 'i' || name[1] == '\0') {
          /* #K 或 #KI - 将整数添加到常量池 */
          luaX_next(ls);
          if (ls->t.token == TK_INT) {
            val = luaK_intK(fs, ls->t.seminfo.i);
            luaX_next(ls);
            return val;
          }
          else if (ls->t.token == '-') {
            luaX_next(ls);
            if (ls->t.token == TK_INT) {
              val = luaK_intK(fs, -ls->t.seminfo.i);
              luaX_next(ls);
              return val;
            }
          }
          luaX_syntaxerror(ls, "integer expected after '#K' in asm");
          return 0;
        }
      }
      luaX_syntaxerror(ls, "invalid constant specifier after '#' in asm");
      return 0;
    }
    else {
      luaX_syntaxerror(ls, "constant expected after '#' in asm");
      return 0;
    }
  }
  else if (ls->t.token == TK_OR) {
    /* @ (TK_OR) 或 @label - PC 位置或标签引用 */
    /* 注意: '@' 在词法分析器中被映射为 TK_OR */
    luaX_next(ls);  /* 跳过 '@' (TK_OR) */
    /* 检查是否是标签引用（必须是纯名称，不能是关键字） */
    if (ls->t.token == TK_NAME && ctx != NULL) {
      /* @label - 引用标签 */
      TString *labelname = ls->t.seminfo.ts;
      /* 检查是否是已定义的标签或常量 */
      int labelIdx = asm_findlabel(ctx, labelname);
      int defIdx = asm_finddefine(ctx, labelname);
      if (labelIdx >= 0 || defIdx < 0) {
        /* 是标签引用（已定义或未定义的标签） */
        int labelpc = asm_reflabel(ls, ctx, labelname);
        luaX_next(ls);  /* 跳过标签名 */
        if (labelpc < 0) {
          /* 前向引用，需要后续修补 */
          if (pendingLabel) *pendingLabel = labelname;
          return 0;  /* 临时返回 0，后续修补 */
        }
        if (isLabelRef) *isLabelRef = 1;
        return labelpc;
      }
      /* 如果是已定义的常量而不是标签，回退让其作为当前PC处理 */
    }
    /* 单独的 @ - 返回当前 PC */
    return fs->pc;
  }
  else if (ls->t.token == TK_NOT) {
    /* !specifier - 特殊值 */
    /* 注意: '!' 在词法分析器中被映射为 TK_NOT */
    luaX_next(ls);  /* 跳过 '!' */
    check(ls, TK_NAME);
    const char *specname = getstr(ls->t.seminfo.ts);
    luaX_next(ls);  /* 跳过标识符 */
    
    if (strcmp(specname, "freereg") == 0) {
      /* !freereg - 当前空闲寄存器编号 */
      return fs->freereg;
    }
    else if (strcmp(specname, "nactvar") == 0) {
      /* !nactvar - 当前活跃局部变量数量 */
      return fs->nactvar;
    }
    else if (strcmp(specname, "pc") == 0) {
      /* !pc - 当前 PC */
      return fs->pc;
    }
    else if (strcmp(specname, "nk") == 0) {
      /* !nk - 当前常量池大小 */
      return fs->nk;
    }
    else if (strcmp(specname, "np") == 0) {
      /* !np - 当前子函数原型数量 */
      return fs->np;
    }
    else {
      luaK_semerror(ls, "unknown special value '!%s' in asm", specname);
      return 0;
    }
  }
  else {
    luaX_syntaxerror(ls, "integer expected in asm instruction");
    return 0;  /* never reached */
  }
}


/*
** 解析汇编指令中的整数参数（简化版，不支持前向标签引用）
*/
static lua_Integer asm_getint (LexState *ls) {
  return asm_getint_ex(ls, NULL, NULL, NULL, NULL);
}


/*
** 尝试解析汇编指令中的可选整数参数
** 如果下一个 token 不是有效的参数格式，则返回默认值
** 支持格式：整数、负号、$varname、^varname、#constant、@、!specifier
** 参数：
**   ls - 词法状态
**   defval - 默认值
** 返回值：
**   解析到的整数值，或默认值
*/
static lua_Integer asm_trygetint (LexState *ls, lua_Integer defval) {
  if (ls->t.token == TK_INT || ls->t.token == '-' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' || 
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%') {
    return asm_getint(ls);
  }
  /* 检查是否是 Rn 格式的寄存器引用 */
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint(ls);
    }
  }
  return defval;
}


/*
** 尝试解析带标签支持的可选整数参数
*/
static lua_Integer asm_trygetint_ex (LexState *ls, AsmContext *ctx,
                                      lua_Integer defval,
                                      int *pendingPc, TString **pendingLabel, int *isLabelRef) {
  if (ls->t.token == TK_INT || ls->t.token == '-' ||
      ls->t.token == TK_DOLLAR || ls->t.token == '^' || 
      ls->t.token == '#' || ls->t.token == TK_OR ||
      ls->t.token == TK_NOT || ls->t.token == '%') {
    return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
  }
  /* 检查是否是 Rn 格式的寄存器引用 */
  if (ls->t.token == TK_NAME) {
    const char *name = getstr(ls->t.seminfo.ts);
    if ((name[0] == 'R' || name[0] == 'r') && name[1] >= '0' && name[1] <= '9') {
      return asm_getint_ex(ls, ctx, pendingPc, pendingLabel, isLabelRef);
    }
  }
  if (pendingPc) *pendingPc = -1;
  if (pendingLabel) *pendingLabel = NULL;
  if (isLabelRef) *isLabelRef = 0;
  return defval;
}


/*
** 前向声明：递归解析 asm 块主体
*/
static void asm_parse_body (LexState *ls, FuncState *fs, AsmContext *ctx, int line);


/*
** 内联汇编语句解析（内部版本，支持嵌套）
** 参数：
**   ls - 词法状态
**   line - 起始行号
**   parent_ctx - 父级上下文（用于嵌套 asm，可为 NULL）
*/
static void asmstat_ex (LexState *ls, int line, AsmContext *parent_ctx);


/*
** 内联汇编语句解析
** 语法: asm( 指令序列 )
** 
** 指令格式:
**   OPCODE A B C [k]    -- iABC 格式
**   OPCODE A Bx         -- iABx 格式
**   OPCODE A sBx        -- iAsBx 格式
**   OPCODE sJ           -- isJ 格式
**   OPCODE Ax           -- iAx 格式
** 
** 辅助语法（可用于任何参数位置）:
**   $varname   - 获取局部变量的寄存器编号
**   ^varname   - 获取 upvalue 的索引
**   #"str"     - 获取字符串常量的常量池索引
**   #123       - 直接返回整数值（用于 LOADI 等）
**   #K 123     - 将整数添加到常量池并返回索引
**   #KF 3.14   - 将浮点数添加到常量池并返回索引
**   @          - 获取当前 PC 位置
**   @label     - 获取标签的 PC 位置（支持前向引用）
**   :label     - 定义标签（标记当前 PC 位置）
**   !freereg   - 当前空闲寄存器编号
**   !nactvar   - 当前活跃局部变量数量
**   !pc        - 当前 PC
**   !nk        - 当前常量池大小
**   !np        - 当前子函数原型数量
** 
** 示例:
**   asm( MOVE $b $a )           -- b = a
**   asm( LOADI $x 100 )         -- x = 100
**   asm( GETUPVAL $local ^upv ) -- local = upvalue
**   asm( LOADK $s #"hello" )    -- s = "hello"
**   asm( LOADK $n #K 42 )       -- n = 常量池中的 42
**   asm( :loop; ... ; JMP @loop ) -- 循环
** 
** 参数：
**   ls - 词法状态
**   line - 起始行号
*/
static void asmstat (LexState *ls, int line) {
  asmstat_ex(ls, line, NULL);
}


static void asmstat_ex (LexState *ls, int line, AsmContext *parent_ctx) {
  FuncState *fs = ls->fs;
  AsmContext ctx;
  
  /* 初始化汇编上下文（动态分配，支持嵌套） */
  asm_initcontext(ls->L, &ctx, parent_ctx);
  
  luaX_next(ls);  /* 跳过 'asm' */
  checknext(ls, '(');
  
  /* 解析指令序列直到遇到 ')' */
  while (ls->t.token != ')') {
    const char *opname;
    int opcode;
    enum OpMode mode;
    Instruction inst;
    int instpc;  /* 当前指令的 PC 位置 */
    TString *pendingLabel = NULL;  /* 待修补的标签 */
    int needsPatch = 0;  /* 是否需要后续修补 */
    int isJumpInst = 0;  /* 是否是跳转指令 */
    
    /*
    ** 跳过注释内容
    ** 支持的注释语法:
    **   ; "注释内容"     - 分号后跟字符串
    **   INSTR args; ; "注释"  - 指令后的分号注释
    **   comment "内容"   - comment/rem 伪指令
    ** 
    ** 当遇到 ';' 时跳过它，如果后面是字符串也跳过
    ** 当遇到单独的字符串时（上一个分号的注释内容），也跳过
    */
    for (;;) {
      if (ls->t.token == ';') {
        luaX_next(ls);  /* 跳过 ';' */
        /* 如果后面是字符串，作为注释内容跳过 */
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);  /* 跳过注释字符串 */
        }
      }
      else if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        /* 单独的字符串，视为上一个分号的注释内容，跳过 */
        luaX_next(ls);
      }
      else {
        break;  /* 不是注释，退出循环 */
      }
    }
    
    /* 再次检查是否到达结束 */
    if (ls->t.token == ')') break;
    
    /* 检查是否是标签定义 */
    if (ls->t.token == ':') {
      luaX_next(ls);  /* 跳过 ':' */
      check(ls, TK_NAME);
      TString *labelname = ls->t.seminfo.ts;
      asm_deflabel(ls, &ctx, labelname, fs->pc, ls->linenumber);
      luaX_next(ls);  /* 跳过标签名 */
      testnext(ls, ';');  /* 可选分号 */
      continue;
    }
    
    /* 获取操作码名称 */
    check(ls, TK_NAME);
    opname = getstr(ls->t.seminfo.ts);
    
    /*
    ** 伪指令处理
    ** comment "str"         - 注释（被忽略）
    ** rep count { ... }     - 循环插入指令
    ** junk "str" / count    - 插入字符串垃圾或随机指令
    ** _if expr ... _endif   - 条件汇编
    ** nop [count]           - 插入NOP指令
    ** raw value             - 直接插入原始指令值
    ** align N               - 对齐到N条指令边界
    ** db/dw/dd value        - 直接嵌入字节/字/双字数据
    ** emit value [,value]   - 发出多个原始指令
    ** def name value        - 定义汇编常量（仅在当前asm块有效）
    */
    
    /*
    ** comment 伪指令: comment "str" 或 comment 任意内容
    ** 用于在 asm 块中添加注释，不生成任何代码
    */
    if (strcmp(opname, "comment") == 0 || strcmp(opname, "rem") == 0 ||
        strcmp(opname, "COMMENT") == 0 || strcmp(opname, "REM") == 0) {
      luaX_next(ls);  /* 跳过 'comment' */
      /* 跳过注释内容（如果是字符串） */
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* nop 伪指令: nop [count] - 插入NOP指令 */
    if (strcmp(opname, "nop") == 0) {
      int nop_count = 1;
      luaX_next(ls);  /* 跳过 'nop' */
      
      /* 可选的计数参数 */
      if (ls->t.token == TK_INT) {
        nop_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
      }
      
      /* 生成NOP指令 (MOVE 0 0 0 0) */
      for (int j = 0; j < nop_count; j++) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* raw 伪指令: raw value - 直接插入原始32位指令值 */
    if (strcmp(opname, "raw") == 0) {
      luaX_next(ls);  /* 跳过 'raw' */
      
      lua_Integer raw_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      Instruction raw_inst = (Instruction)raw_val;
      luaK_code(fs, raw_inst);
      luaK_fixline(fs, line);
      
      testnext(ls, ';');
      continue;
    }
    
    /* emit 伪指令: emit value [, value, ...] - 发出多个原始指令 */
    if (strcmp(opname, "emit") == 0) {
      luaX_next(ls);  /* 跳过 'emit' */
      
      do {
        lua_Integer emit_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        Instruction emit_inst = (Instruction)emit_val;
        luaK_code(fs, emit_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      
      testnext(ls, ';');
      continue;
    }
    
    /*
    ** 嵌套 asm 伪指令: asm( ... )
    ** 支持在 asm 块内部嵌套另一个 asm 块
    ** 内层 asm 块继承外层的常量定义（通过 parent 指针）
    ** 但拥有独立的标签和待修补列表
    ** 
    ** 使用递归调用 asm_parse_body 实现真正的嵌套支持
    */
    if (strcmp(opname, "asm") == 0) {
      int nested_line = ls->linenumber;
      AsmContext nested_ctx;
      
      luaX_next(ls);  /* 跳过 'asm' */
      checknext(ls, '(');
      
      /* 创建子上下文，继承父级的常量定义 */
      asm_initcontext(ls->L, &nested_ctx, &ctx);
      
      /* 递归解析嵌套 asm 块的主体 - 使用与主循环相同的逻辑 */
      asm_parse_body(ls, fs, &nested_ctx, nested_line);
      
      /* 修补并释放子上下文 */
      asm_patchpending(ls, fs, &nested_ctx);
      asm_freecontext(ls->L, &nested_ctx);
      
      checknext(ls, ')');
      testnext(ls, ';');
      continue;
    }
    
    /*
    ** jmpx 伪指令: jmpx @label - 跳转到标签（自动计算相对偏移）
    ** 解决后向跳转时 @label 返回绝对 PC 的问题
    ** 参数：
    **   @label - 目标标签
    ** 功能：
    **   自动计算从当前 PC 到目标标签的相对偏移，生成正确的 JMP 指令
    */
    if (strcmp(opname, "jmpx") == 0 || strcmp(opname, "JMPX") == 0) {
      int target_pc;
      int current_pc;
      int offset;
      TString *label = NULL;
      
      luaX_next(ls);  /* 跳过 'jmpx' */
      
      /* 必须是 @label 格式 */
      if (ls->t.token != TK_OR) {
        luaK_semerror(ls, "jmpx requires @label argument");
      }
      luaX_next(ls);  /* 跳过 '@' (TK_OR) */
      
      check(ls, TK_NAME);
      label = ls->t.seminfo.ts;
      
      /* 查找标签 */
      int labelIdx = asm_findlabel(&ctx, label);
      if (labelIdx >= 0 && ctx.labels[labelIdx].pc >= 0) {
        /* 后向引用：标签已定义，直接计算偏移 */
        target_pc = ctx.labels[labelIdx].pc;
        current_pc = fs->pc;
        offset = target_pc - (current_pc + 1);  /* 相对于下一条指令的偏移 */
        
        /* 生成 JMP 指令 */
        Instruction jmp_inst = CREATE_sJ(OP_JMP, offset + OFFSET_sJ, 0);
        luaK_code(fs, jmp_inst);
        luaK_fixline(fs, line);
      }
      else {
        /* 前向引用：标签未定义，需要后续修补 */
        int instpc = fs->pc;
        Instruction jmp_inst = CREATE_sJ(OP_JMP, OFFSET_sJ, 0);  /* 临时偏移为0 */
        luaK_code(fs, jmp_inst);
        luaK_fixline(fs, line);
        
        /* 添加到待修补列表 */
        asm_addpending(ls, &ctx, label, instpc, ls->linenumber, 1);
      }
      
      luaX_next(ls);  /* 跳过标签名 */
      testnext(ls, ';');
      continue;
    }
    
    /* align 伪指令: align N - 用NOP对齐到N条指令边界 */
    if (strcmp(opname, "align") == 0) {
      int align_val;
      luaX_next(ls);  /* 跳过 'align' */
      
      align_val = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      if (align_val < 1) {
        luaK_semerror(ls, "align value must be positive");
      }
      
      /* 插入NOP直到PC对齐 */
      while (fs->pc % align_val != 0) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* def 伪指令: def name value - 定义汇编常量（仅在当前asm块有效）*/
    if (strcmp(opname, "def") == 0 || strcmp(opname, "define") == 0) {
      TString *def_name;
      lua_Integer def_value;
      luaX_next(ls);  /* 跳过 'def' */
      
      check(ls, TK_NAME);
      def_name = ls->t.seminfo.ts;
      luaX_next(ls);  /* 跳过常量名 */
      
      /* 获取常量值 */
      def_value = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      
      /* 添加到常量定义表 */
      asm_adddefine(ls, &ctx, def_name, def_value);
      
      testnext(ls, ';');
      continue;
    }
    
    /* 
    ** 调试辅助伪指令
    ** _print "msg" [value]  - 编译时打印消息和可选值
    ** _assert cond ["msg"]  - 编译时断言，条件为假时报错
    ** _info                 - 打印当前编译状态（PC、寄存器等）
    */
    if (strcmp(opname, "_print") == 0 || strcmp(opname, "asmprint") == 0) {
      luaX_next(ls);  /* 跳过 '_print' */
      
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        const char *msg = getstr(ls->t.seminfo.ts);
        luaX_next(ls);
        
        /* 检查是否有可选的值参数 */
        /* 注意: '@' -> TK_OR, '$' -> TK_DOLLAR, '!' -> TK_NOT */
        /* TK_NAME 用于支持 def 定义的常量 */
        if (ls->t.token == TK_INT || ls->t.token == '-' ||
            ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
            ls->t.token == TK_NOT || ls->t.token == TK_OR ||
            ls->t.token == TK_NAME) {
          lua_Integer val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          printf("[ASM] %s: %lld\n", msg, (long long)val);
        }
        else {
          printf("[ASM] %s\n", msg);
        }
      }
      else if (ls->t.token == TK_INT || ls->t.token == '-' ||
               ls->t.token == TK_DOLLAR || ls->t.token == '%' ||
               ls->t.token == TK_NOT || ls->t.token == TK_OR ||
               ls->t.token == TK_NAME) {
        /* 没有消息，直接打印值 */
        lua_Integer val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        printf("[ASM] value: %lld\n", (long long)val);
      }
      else {
        luaK_semerror(ls, "_print expects string or value");
      }
      
      testnext(ls, ';');
      continue;
    }
    
    if (strcmp(opname, "_assert") == 0 || strcmp(opname, "asmassert") == 0) {
      int cond_result;
      lua_Integer left_val, right_val;
      luaX_next(ls);  /* 跳过 '_assert' */
      
      /* 解析左操作数 */
      left_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      
      /* 检查是否有比较运算符 */
      if (ls->t.token == TK_EQ) {  /* == */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {  /* ~= 或 != */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {  /* >= */
          luaX_next(ls);
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        }
        else {  /* > */
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {  /* <= */
          luaX_next(ls);
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        }
        else {  /* < */
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {  /* >= */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {  /* <= */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        /* 没有比较运算符，只检查值是否非零 */
        cond_result = (left_val != 0);
      }
      
      if (cond_result == 0) {
        /* 断言失败 */
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          const char *msg = getstr(ls->t.seminfo.ts);
          luaX_next(ls);
          luaK_semerror(ls, "asm assertion failed: %s", msg);
        }
        else {
          luaK_semerror(ls, "asm assertion failed");
        }
      }
      else {
        /* 断言成功，跳过可选的消息 */
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      
      testnext(ls, ';');
      continue;
    }
    
    if (strcmp(opname, "_info") == 0 || strcmp(opname, "asminfo") == 0) {
      luaX_next(ls);  /* 跳过 '_info' */
      
      printf("[ASM INFO] line=%d, pc=%d, freereg=%d, nactvar=%d, nk=%d\n",
             ls->linenumber, fs->pc, fs->freereg, fs->nactvar, fs->nk);
      
      testnext(ls, ';');
      continue;
    }
    
    /* db 伪指令: db byte1 [, byte2, ...] - 嵌入字节数据（4字节打包为一条指令）*/
    if (strcmp(opname, "db") == 0) {
      unsigned char bytes[4] = {0, 0, 0, 0};
      int byte_count = 0;
      luaX_next(ls);  /* 跳过 'db' */
      
      do {
        lua_Integer byte_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        if (byte_count < 4) {
          bytes[byte_count++] = (unsigned char)(byte_val & 0xFF);
        }
        /* 每4字节发出一条指令 */
        if (byte_count == 4) {
          Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
          luaK_code(fs, db_inst);
          luaK_fixline(fs, line);
          byte_count = 0;
          memset(bytes, 0, 4);
        }
      } while (testnext(ls, ','));
      
      /* 剩余字节也发出 */
      if (byte_count > 0) {
        Instruction db_inst = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
        luaK_code(fs, db_inst);
        luaK_fixline(fs, line);
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* dw 伪指令: dw word1 [, word2] - 嵌入16位字数据（2个字打包为一条指令）*/
    if (strcmp(opname, "dw") == 0) {
      unsigned short words[2] = {0, 0};
      int word_count = 0;
      luaX_next(ls);  /* 跳过 'dw' */
      
      do {
        lua_Integer word_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        if (word_count < 2) {
          words[word_count++] = (unsigned short)(word_val & 0xFFFF);
        }
        /* 每2个字发出一条指令 */
        if (word_count == 2) {
          Instruction dw_inst = words[0] | (words[1] << 16);
          luaK_code(fs, dw_inst);
          luaK_fixline(fs, line);
          word_count = 0;
          memset(words, 0, 4);
        }
      } while (testnext(ls, ','));
      
      /* 剩余数据也发出 */
      if (word_count > 0) {
        Instruction dw_inst = words[0] | (words[1] << 16);
        luaK_code(fs, dw_inst);
        luaK_fixline(fs, line);
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* dd 伪指令: dd dword - 嵌入32位双字（直接作为一条指令）*/
    if (strcmp(opname, "dd") == 0) {
      luaX_next(ls);  /* 跳过 'dd' */
      
      do {
        lua_Integer dword_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        Instruction dd_inst = (Instruction)(dword_val & 0xFFFFFFFF);
        luaK_code(fs, dd_inst);
        luaK_fixline(fs, line);
      } while (testnext(ls, ','));
      
      testnext(ls, ';');
      continue;
    }
    
    /* str 伪指令: str "string" - 将字符串直接编码为指令序列（每4字节一条指令）*/
    if (strcmp(opname, "str") == 0) {
      luaX_next(ls);  /* 跳过 'str' */
      
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        TString *str_data = ls->t.seminfo.ts;
        const char *str = getstr(str_data);
        size_t len = tsslen(str_data);
        size_t i;
        
        /* 每4字节生成一条原始指令 */
        for (i = 0; i < len; i += 4) {
          unsigned int data = 0;
          data |= ((unsigned char)str[i]) << 0;
          if (i + 1 < len) data |= ((unsigned char)str[i + 1]) << 8;
          if (i + 2 < len) data |= ((unsigned char)str[i + 2]) << 16;
          if (i + 3 < len) data |= ((unsigned char)str[i + 3]) << 24;
          
          Instruction str_inst = (Instruction)data;
          luaK_code(fs, str_inst);
          luaK_fixline(fs, line);
        }
        
        luaX_next(ls);
      }
      else {
        luaK_semerror(ls, "str expects a string literal");
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* rep 伪指令: rep count { instructions } */
    if (strcmp(opname, "rep") == 0 || strcmp(opname, "repeat") == 0) {
      int rep_count;
      int rep_start_pc;
      int rep_end_pc;
      int instr_count;
      int i;
      
      luaX_next(ls);  /* 跳过 'rep' */
      
      /* 获取循环次数 */
      rep_count = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      if (rep_count < 0) {
        luaK_semerror(ls, "rep count must be non-negative");
      }
      
      checknext(ls, '{');
      
      /* 记录第一次生成指令的起始位置 */
      rep_start_pc = fs->pc;
      
      /* 第一次解析并生成指令 */
      while (ls->t.token != '}') {
        const char *inner_opname;
        int inner_opcode;
        enum OpMode inner_mode;
        Instruction inner_inst;
        int inner_instpc;
        TString *inner_pendingLabel = NULL;
        int inner_needsPatch = 0;
        int inner_isJump = 0;
        
        /* 处理内部标签定义 */
        if (ls->t.token == ':') {
          luaX_next(ls);
          check(ls, TK_NAME);
          TString *inner_labelname = ls->t.seminfo.ts;
          asm_deflabel(ls, &ctx, inner_labelname, fs->pc, ls->linenumber);
          luaX_next(ls);
          testnext(ls, ';');
          continue;
        }
        
        check(ls, TK_NAME);
        inner_opname = getstr(ls->t.seminfo.ts);
        inner_opcode = find_opcode(inner_opname);
        
        if (inner_opcode < 0) {
          luaK_semerror(ls, "unknown opcode '%s' in asm rep block", inner_opname);
        }
        
        luaX_next(ls);
        inner_mode = getOpMode(inner_opcode);
        inner_instpc = fs->pc;
        
        /* 根据格式解析参数并生成指令 */
        switch (inner_mode) {
          case iABC: {
            int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
            int b = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, &inner_pendingLabel, NULL);
            if (inner_pendingLabel) inner_needsPatch = 1;
            int c = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, inner_pendingLabel ? NULL : &inner_pendingLabel, NULL);
            if (inner_pendingLabel && !inner_needsPatch) inner_needsPatch = 1;
            int k = (int)asm_trygetint(ls, 0);
            inner_inst = CREATE_ABCk(inner_opcode, a, b, c, k);
            break;
          }
          case ivABC: {
            int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
            int vb = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, &inner_pendingLabel, NULL);
            if (inner_pendingLabel) inner_needsPatch = 1;
            int vc = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, inner_pendingLabel ? NULL : &inner_pendingLabel, NULL);
            if (inner_pendingLabel && !inner_needsPatch) inner_needsPatch = 1;
            int k = (int)asm_trygetint(ls, 0);
            inner_inst = CREATE_vABCk(inner_opcode, a, vb, vc, k);
            break;
          }
          case iABx: {
            int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
            int isLabelRef = 0;
            unsigned int bx = (unsigned int)asm_getint_ex(ls, &ctx, NULL, &inner_pendingLabel, &isLabelRef);
            if (inner_pendingLabel) {
              inner_needsPatch = 1;
              if (inner_opcode == OP_FORLOOP || inner_opcode == OP_TFORLOOP ||
                  inner_opcode == OP_FORPREP || inner_opcode == OP_TFORPREP) {
                inner_isJump = 1;
              }
            } else if (isLabelRef) {
               int offset;
               int target = (int)bx;
               if (inner_opcode == OP_FORLOOP || inner_opcode == OP_TFORLOOP) {
                 offset = (inner_instpc + 1) - target;
                 if (offset <= 0) luaK_semerror(ls, "jump target for loop instruction must be backward");
                 bx = (unsigned int)offset;
               } else if (inner_opcode == OP_FORPREP || inner_opcode == OP_TFORPREP) {
                 offset = target - (inner_instpc + 1);
                 if (offset < 0) luaK_semerror(ls, "jump target for prep instruction must be forward");
                 if (inner_opcode == OP_FORPREP) offset--;
                 bx = (unsigned int)offset;
               }
            }
            inner_inst = CREATE_ABx(inner_opcode, a, bx);
            break;
          }
          case iAsBx: {
            int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
            int sbx = (int)asm_getint_ex(ls, &ctx, NULL, &inner_pendingLabel, NULL);
            if (inner_pendingLabel) { inner_needsPatch = 1; inner_isJump = 1; }
            inner_inst = CREATE_ABx(inner_opcode, a, cast_uint(sbx + OFFSET_sBx));
            break;
          }
          case iAx: {
            int ax = (int)asm_getint_ex(ls, &ctx, NULL, &inner_pendingLabel, NULL);
            if (inner_pendingLabel) inner_needsPatch = 1;
            inner_inst = CREATE_Ax(inner_opcode, ax);
            break;
          }
          case isJ: {
            int sj = (int)asm_getint_ex(ls, &ctx, NULL, &inner_pendingLabel, NULL);
            if (inner_pendingLabel) { inner_needsPatch = 1; inner_isJump = 1; }
            inner_inst = CREATE_sJ(inner_opcode, sj + OFFSET_sJ, 0);
            break;
          }
          default:
            inner_inst = 0;
            break;
        }
        
        luaK_code(fs, inner_inst);
        luaK_fixline(fs, line);
        
        if (inner_needsPatch && inner_pendingLabel) {
          asm_addpending(ls, &ctx, inner_pendingLabel, inner_instpc, ls->linenumber, inner_isJump);
        }
        
        testnext(ls, ';');
      }
      
      rep_end_pc = fs->pc;
      instr_count = rep_end_pc - rep_start_pc;
      
      checknext(ls, '}');
      
      /* 复制指令 rep_count - 1 次 */
      for (i = 1; i < rep_count; i++) {
        int j;
        for (j = 0; j < instr_count; j++) {
          Instruction copied_inst = fs->f->code[rep_start_pc + j];
          luaK_code(fs, copied_inst);
          luaK_fixline(fs, line);
        }
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* junk 伪指令: junk "string" 或 junk count */
    /*
    ** junk "string" - 将字符串的原始字节直接编码为 EXTRAARG 指令序列
    **                 每条 EXTRAARG 指令可以存储 26 位数据（约3.25字节）
    **                 这样字符串数据直接嵌入字节码，不经过常量池，不受加密影响
    ** junk count    - 生成 count 条 MOVE 0 0 作为 NOP 填充
    */
    if (strcmp(opname, "junk") == 0 || strcmp(opname, "garbage") == 0) {
      luaX_next(ls);  /* 跳过 'junk' */
      
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        /* junk "string" - 将字符串原始字节直接编码为指令序列 */
        TString *junk_str = ls->t.seminfo.ts;
        const char *str = getstr(junk_str);
        size_t len = tsslen(junk_str);
        size_t i;
        
        /*
        ** 将字符串数据编码为 EXTRAARG 指令序列
        ** EXTRAARG 指令格式: [opcode:7][Ax:25]
        ** 我们使用 Ax 字段存储原始数据（每条指令存储3字节 + 1位标记）
        ** 第一条指令的 Ax 存储字符串长度
        */
        
        /* 首先生成一条存储长度的 EXTRAARG */
        {
          Instruction len_inst = CREATE_Ax(OP_EXTRAARG, (int)(len & MAXARG_Ax));
          luaK_code(fs, len_inst);
          luaK_fixline(fs, line);
        }
        
        /* 然后每3字节生成一条 EXTRAARG 指令 */
        for (i = 0; i < len; i += 3) {
          unsigned int data = 0;
          /* 打包最多3个字节到 data 中 */
          data |= ((unsigned char)str[i]) << 0;
          if (i + 1 < len) data |= ((unsigned char)str[i + 1]) << 8;
          if (i + 2 < len) data |= ((unsigned char)str[i + 2]) << 16;
          
          /* 确保不超过 MAXARG_Ax (25位) */
          data &= MAXARG_Ax;
          
          Instruction data_inst = CREATE_Ax(OP_EXTRAARG, (int)data);
          luaK_code(fs, data_inst);
          luaK_fixline(fs, line);
        }
        
        luaX_next(ls);
      }
      else if (ls->t.token == TK_INT) {
        /* junk count - 生成count条NOP指令（MOVE 0 0 0 0）作为填充 */
        int junk_count = (int)ls->t.seminfo.i;
        int j;
        luaX_next(ls);
        
        if (junk_count < 0) {
          luaK_semerror(ls, "junk count must be non-negative");
        }
        
        for (j = 0; j < junk_count; j++) {
          /* 生成 MOVE 0 0 作为无意义指令 */
          Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
          luaK_code(fs, nop_inst);
          luaK_fixline(fs, line);
        }
      }
      else {
        luaK_semerror(ls, "junk expects a string or integer count");
      }
      
      testnext(ls, ';');
      continue;
    }
    
    /* _if 伪指令: _if expr [op expr2] ... _endif (编译时条件汇编) */
    /*
    ** 支持的条件格式：
    **   _if value              - 如果value非零则为真
    **   _if val1 == val2       - 相等比较
    **   _if val1 != val2       - 不等比较
    **   _if val1 > val2        - 大于比较
    **   _if val1 < val2        - 小于比较
    **   _if val1 >= val2       - 大于等于比较
    **   _if val1 <= val2       - 小于等于比较
    */
    if (strcmp(opname, "_if") == 0 || strcmp(opname, "asmif") == 0) {
      int cond_result;
      lua_Integer left_val, right_val;
      
      luaX_next(ls);  /* 跳过 '_if' */
      
      /* 解析左操作数 */
      left_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
      
      /* 检查是否有比较运算符 */
      if (ls->t.token == TK_EQ) {  /* == */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val == right_val);
      }
      else if (ls->t.token == TK_NE) {  /* ~= 或 != */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val != right_val);
      }
      else if (ls->t.token == '>') {
        luaX_next(ls);
        if (ls->t.token == '=') {  /* >= */
          luaX_next(ls);
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val >= right_val);
        }
        else {  /* > */
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val > right_val);
        }
      }
      else if (ls->t.token == '<') {
        luaX_next(ls);
        if (ls->t.token == '=') {  /* <= */
          luaX_next(ls);
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val <= right_val);
        }
        else {  /* < */
          right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
          cond_result = (left_val < right_val);
        }
      }
      else if (ls->t.token == TK_GE) {  /* >= */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val >= right_val);
      }
      else if (ls->t.token == TK_LE) {  /* <= */
        luaX_next(ls);
        right_val = asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        cond_result = (left_val <= right_val);
      }
      else {
        /* 没有比较运算符，只检查值是否非零 */
        cond_result = (left_val != 0);
      }
      
      /* 如果条件为假，跳过直到 _endif */
      if (!cond_result) {
        int nest_level = 1;
        while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
          if (ls->t.token == TK_NAME) {
            const char *name = getstr(ls->t.seminfo.ts);
            if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
              nest_level++;
            }
            else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
              nest_level--;
            }
            else if (nest_level == 1 && (strcmp(name, "_else") == 0 || strcmp(name, "asmelse") == 0)) {
              /* 在同级遇到 _else，跳出循环并执行 else 分支 */
              luaX_next(ls);  /* 跳过 '_else' */
              testnext(ls, ';');
              nest_level = 0;  /* 退出跳过循环 */
              break;
            }
          }
          if (nest_level > 0) luaX_next(ls);
        }
      }
      /* 条件为真则继续正常解析指令 */
      testnext(ls, ';');
      continue;
    }
    
    /* _else 伪指令 - 跳过到 _endif */
    if (strcmp(opname, "_else") == 0 || strcmp(opname, "asmelse") == 0) {
      int nest_level = 1;
      luaX_next(ls);  /* 跳过 '_else' */
      /* 条件为真时遇到 _else，需要跳过直到 _endif */
      while (nest_level > 0 && ls->t.token != TK_EOS && ls->t.token != ')') {
        if (ls->t.token == TK_NAME) {
          const char *name = getstr(ls->t.seminfo.ts);
          if (strcmp(name, "_if") == 0 || strcmp(name, "asmif") == 0) {
            nest_level++;
          }
          else if (strcmp(name, "_endif") == 0 || strcmp(name, "asmend") == 0) {
            nest_level--;
          }
        }
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* _endif 伪指令 */
    if (strcmp(opname, "_endif") == 0 || strcmp(opname, "asmend") == 0) {
      luaX_next(ls);  /* 跳过 '_endif' */
      testnext(ls, ';');
      continue;
    }
    
    opcode = find_opcode(opname);
    
    if (opcode < 0) {
      luaK_semerror(ls, "unknown opcode '%s' in asm", opname);
    }
    
    luaX_next(ls);  /* 跳过操作码名称 */
    
    /* 根据操作码格式解析参数 */
    mode = getOpMode(opcode);
    instpc = fs->pc;  /* 记录当前 PC */
    
    switch (mode) {
      case iABC: {
        /* iABC 格式: A [B] [C] [k] - B、C、k 参数可选，默认为 0 */
        int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        int b = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int c = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        
        /* 参数范围检查 */
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, b, MAXARG_B, "B");
        asm_checkrange(ls, c, MAXARG_C, "C");
        asm_checkrange(ls, k, 1, "k");
        
        /*
        ** 某些指令使用带符号的 sB 或 sC 参数，需要进行偏移转换
        ** sB 指令: GTI, GEI, LTI, LEI, EQI, MMBINI
        ** sC 指令: ADDI, SHLI, SHRI
        */
        if (opcode == OP_GTI || opcode == OP_GEI || 
            opcode == OP_LTI || opcode == OP_LEI || 
            opcode == OP_EQI || opcode == OP_MMBINI) {
          /* B 是 sB (带符号立即数) */
          asm_checkrange_signed(ls, b, -OFFSET_sC, OFFSET_sC, "sB");
          b = int2sC(b);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else if (opcode == OP_ADDI || opcode == OP_SHLI || opcode == OP_SHRI) {
          /* C 是 sC (带符号立即数) */
          asm_checkrange_signed(ls, c, -OFFSET_sC, OFFSET_sC, "sC");
          c = int2sC(c);
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        else {
          inst = CREATE_ABCk(opcode, a, b, c, k);
        }
        break;
      }
      case ivABC: {
        /* ivABC 格式: A [vB] [vC] [k] - vB、vC、k 参数可选，默认为 0 */
        int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        int vb = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int vc = (int)asm_trygetint_ex(ls, &ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        
        /* 参数范围检查 */
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, vb, MAXARG_vB, "vB");
        asm_checkrange(ls, vc, MAXARG_vC, "vC");
        asm_checkrange(ls, k, 1, "k");
        
        inst = CREATE_vABCk(opcode, a, vb, vc, k);
        break;
      }
      case iABx: {
        /* iABx 格式: A Bx */
        int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        int isLabelRef = 0;
        unsigned int bx = (unsigned int)asm_getint_ex(ls, &ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP ||
              opcode == OP_FORPREP || opcode == OP_TFORPREP) {
            isJumpInst = 1;
          }
        } else if (isLabelRef) {
          /* 标签已解析，如果是跳转指令，需要计算相对偏移 */
          int offset;
          int target = (int)bx;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP) {
             /* Backward: Bx = (pc + 1) - target */
             offset = (instpc + 1) - target;
             if (offset <= 0) luaK_semerror(ls, "jump target for loop instruction must be backward");
             bx = (unsigned int)offset;
          } else if (opcode == OP_FORPREP || opcode == OP_TFORPREP) {
             /* Forward: Bx = target - (pc + 1) */
             offset = target - (instpc + 1);
             if (offset < 0) luaK_semerror(ls, "jump target for prep instruction must be forward");
             if (opcode == OP_FORPREP) offset--; /* pc += Bx + 1 */
             bx = (unsigned int)offset;
          }
        }
        
        /* 参数范围检查 */
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange(ls, bx, MAXARG_Bx, "Bx");
        
        inst = CREATE_ABx(opcode, a, bx);
        break;
      }
      case iAsBx: {
        /* iAsBx 格式: A sBx (带符号偏移) */
        int a = (int)asm_getint_ex(ls, &ctx, NULL, NULL, NULL);
        int sbx = (int)asm_getint_ex(ls, &ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;  /* FORLOOP, FORPREP 等使用相对偏移 */
        }
        
        /* 参数范围检查 */
        asm_checkrange(ls, a, MAXARG_A, "A");
        asm_checkrange_signed(ls, sbx, -OFFSET_sBx, OFFSET_sBx, "sBx");
        
        /* sBx 需要加上偏移量 OFFSET_sBx 转换为无符号存储 */
        inst = CREATE_ABx(opcode, a, cast_uint(sbx + OFFSET_sBx));
        break;
      }
      case iAx: {
        /* iAx 格式: Ax */
        int ax = (int)asm_getint_ex(ls, &ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        
        /* 参数范围检查 */
        asm_checkrange(ls, ax, MAXARG_Ax, "Ax");
        
        inst = CREATE_Ax(opcode, ax);
        break;
      }
      case isJ: {
        /* isJ 格式: sJ (带符号跳转偏移) */
        int isLabelRef = 0;
        int sj = (int)asm_getint_ex(ls, &ctx, NULL, &pendingLabel, &isLabelRef);
        if (pendingLabel) {
          needsPatch = 1;
          isJumpInst = 1;  /* JMP 使用相对偏移 */
        } else if (isLabelRef) {
          /* 标签已解析: sj 是目标 PC，需转为 offset */
          /* offset = target - (pc + 1) */
          sj = sj - (instpc + 1);
        }
        
        /* 参数范围检查 */
        asm_checkrange_signed(ls, sj, -OFFSET_sJ, OFFSET_sJ, "sJ");
        
        /* sJ 需要加上偏移量 OFFSET_sJ 转换为无符号存储 */
        inst = CREATE_sJ(opcode, sj + OFFSET_sJ, 0);
        break;
      }
      default: {
        luaK_semerror(ls, "unsupported opcode mode in asm");
        inst = 0;  /* never reached */
      }
    }
    
    /* 直接将指令写入代码流 */
    luaK_code(fs, inst);
    luaK_fixline(fs, line);
    
    /* 如果有前向标签引用，添加到待修补列表 */
    if (needsPatch && pendingLabel) {
      asm_addpending(ls, &ctx, pendingLabel, instpc, ls->linenumber, isJumpInst);
    }
    
    /*
    ** 算术指令需要紧跟 MMBIN/MMBINI/MMBINK 指令
    ** 因为 Lua 5.4 VM 中算术成功时会 pc++ 跳过下一条指令
    ** 如果不生成 MMBIN，后续正常代码会被错误跳过
    */
    if (opcode >= OP_ADD && opcode <= OP_SHR) {
      /* 寄存器-寄存器算术: 生成 OP_MMBIN */
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADD) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBIN, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_ADDI) {
      /* ADDI: 生成 OP_MMBINI，B 参数是已转换的 sC 值 */
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);  /* 直接用 C 字段的值，已经是 int2sC 转换过的 */
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_ADD, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHLI) {
      /* SHLI: 生成 OP_MMBINI */
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHL, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_SHRI) {
      /* SHRI: 生成 OP_MMBINI */
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      luaK_codeABCk(fs, OP_MMBINI, b, sc, TM_SHR, 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_ADDK && opcode <= OP_IDIVK) {
      /* ADDK-IDIVK: 生成 OP_MMBINK */
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADDK) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_BANDK && opcode <= OP_BXORK) {
      /* BANDK-BXORK: 生成 OP_MMBINK */
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_BANDK) + TM_BAND);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    
    /* 如果指令会写入寄存器 A，需要更新 freereg 和 maxstacksize 以防止后续代码覆盖 */
    if (testAMode(opcode)) {
      int a = GETARG_A(inst);
      if (a >= fs->freereg) {
        /* 计算需要扩展的寄存器数量 */
        int needed = a + 1 - fs->freereg;
        /* 检查并更新 maxstacksize */
        luaK_checkstack(fs, needed);
        fs->freereg = cast_byte(a + 1);
      }
    }
    
    /* 可选的分号或换行分隔 */
    testnext(ls, ';');
  }
  
  /* 修补所有待处理的前向引用 */
  asm_patchpending(ls, fs, &ctx);
  
  /* 释放汇编上下文 */
  asm_freecontext(ls->L, &ctx);
  
  checknext(ls, ')');
}


/*
** 递归解析 asm 块主体
** 这个函数包含 asm 主循环的完整逻辑，支持所有伪指令和嵌套
** 参数：
**   ls - 词法状态
**   fs - 函数状态
**   ctx - 当前汇编上下文
**   line - 行号
*/
static void asm_parse_body (LexState *ls, FuncState *fs, AsmContext *ctx, int line) {
  /* 解析指令序列直到遇到 ')' */
  while (ls->t.token != ')') {
    const char *opname;
    int opcode;
    enum OpMode mode;
    Instruction inst;
    int instpc;
    TString *pendingLabel = NULL;
    int needsPatch = 0;
    int isJumpInst = 0;
    
    /* 跳过注释内容 */
    for (;;) {
      if (ls->t.token == ';') {
        luaX_next(ls);
        if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
          luaX_next(ls);
        }
      }
      else if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      else {
        break;
      }
    }
    
    if (ls->t.token == ')') break;
    
    /* 标签定义 */
    if (ls->t.token == ':') {
      luaX_next(ls);
      check(ls, TK_NAME);
      TString *labelname = ls->t.seminfo.ts;
      asm_deflabel(ls, ctx, labelname, fs->pc, ls->linenumber);
      luaX_next(ls);
      testnext(ls, ';');
      continue;
    }
    
    check(ls, TK_NAME);
    opname = getstr(ls->t.seminfo.ts);
    
    /* comment/rem 伪指令 */
    if (strcmp(opname, "comment") == 0 || strcmp(opname, "rem") == 0 ||
        strcmp(opname, "COMMENT") == 0 || strcmp(opname, "REM") == 0) {
      luaX_next(ls);
      if (ls->t.token == TK_STRING || ls->t.token == TK_RAWSTRING) {
        luaX_next(ls);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* nop 伪指令 */
    if (strcmp(opname, "nop") == 0) {
      int nop_count = 1;
      luaX_next(ls);
      if (ls->t.token == TK_INT) {
        nop_count = (int)ls->t.seminfo.i;
        luaX_next(ls);
      }
      for (int j = 0; j < nop_count; j++) {
        Instruction nop_inst = CREATE_ABCk(OP_MOVE, 0, 0, 0, 0);
        luaK_code(fs, nop_inst);
        luaK_fixline(fs, line);
      }
      testnext(ls, ';');
      continue;
    }
    
    /* def 伪指令 */
    if (strcmp(opname, "def") == 0 || strcmp(opname, "define") == 0) {
      TString *def_name;
      lua_Integer def_value;
      luaX_next(ls);
      check(ls, TK_NAME);
      def_name = ls->t.seminfo.ts;
      luaX_next(ls);
      def_value = asm_getint_ex(ls, ctx, NULL, NULL, NULL);
      asm_adddefine(ls, ctx, def_name, def_value);
      testnext(ls, ';');
      continue;
    }
    
    /* 嵌套 asm 伪指令 - 递归调用 */
    if (strcmp(opname, "asm") == 0) {
      int nested_line = ls->linenumber;
      AsmContext nested_ctx;
      
      luaX_next(ls);  /* 跳过 'asm' */
      checknext(ls, '(');
      
      asm_initcontext(ls->L, &nested_ctx, ctx);
      asm_parse_body(ls, fs, &nested_ctx, nested_line);  /* 递归 */
      asm_patchpending(ls, fs, &nested_ctx);
      asm_freecontext(ls->L, &nested_ctx);
      
      checknext(ls, ')');
      testnext(ls, ';');
      continue;
    }
    
    /* 查找操作码 */
    opcode = find_opcode(opname);
    if (opcode < 0) {
      luaK_semerror(ls, "unknown opcode '%s' in asm", opname);
    }
    
    luaX_next(ls);
    mode = getOpMode(opcode);
    instpc = fs->pc;
    
    /* 根据操作码格式解析参数 */
    switch (mode) {
      case iABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int b = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int c = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        
        if (opcode == OP_GTI || opcode == OP_GEI || 
            opcode == OP_LTI || opcode == OP_LEI || 
            opcode == OP_EQI || opcode == OP_MMBINI) {
          b = int2sC(b);
        }
        else if (opcode == OP_ADDI || opcode == OP_SHLI || opcode == OP_SHRI) {
          c = int2sC(c);
        }
        inst = CREATE_ABCk(opcode, a, b, c, k);
        break;
      }
      case ivABC: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int vb = (int)asm_trygetint_ex(ls, ctx, 0, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        int vc = (int)asm_trygetint_ex(ls, ctx, 0, NULL, pendingLabel ? NULL : &pendingLabel, NULL);
        if (pendingLabel && !needsPatch) needsPatch = 1;
        int k = (int)asm_trygetint(ls, 0);
        inst = CREATE_vABCk(opcode, a, vb, vc, k);
        break;
      }
      case iABx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        unsigned int bx = (unsigned int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) {
          needsPatch = 1;
          if (opcode == OP_FORLOOP || opcode == OP_TFORLOOP ||
              opcode == OP_FORPREP || opcode == OP_TFORPREP) {
            isJumpInst = 1;
          }
        }
        inst = CREATE_ABx(opcode, a, bx);
        break;
      }
      case iAsBx: {
        int a = (int)asm_getint_ex(ls, ctx, NULL, NULL, NULL);
        int sbx = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) { needsPatch = 1; isJumpInst = 1; }
        inst = CREATE_ABx(opcode, a, cast_uint(sbx + OFFSET_sBx));
        break;
      }
      case iAx: {
        int ax = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) needsPatch = 1;
        inst = CREATE_Ax(opcode, ax);
        break;
      }
      case isJ: {
        int sj = (int)asm_getint_ex(ls, ctx, NULL, &pendingLabel, NULL);
        if (pendingLabel) { needsPatch = 1; isJumpInst = 1; }
        inst = CREATE_sJ(opcode, sj + OFFSET_sJ, 0);
        break;
      }
      default:
        inst = 0;
        break;
    }
    
    luaK_code(fs, inst);
    luaK_fixline(fs, line);
    
    if (needsPatch && pendingLabel) {
      asm_addpending(ls, ctx, pendingLabel, instpc, ls->linenumber, isJumpInst);
    }
    
    /* 自动生成 MMBIN 系列指令 */
    if (opcode >= OP_ADD && opcode <= OP_SHR) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADD) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBIN, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode == OP_ADDI || opcode == OP_SHLI || opcode == OP_SHRI) {
      int b = GETARG_B(inst);
      int sc = GETARG_C(inst);
      TMS tm = (opcode == OP_ADDI) ? TM_ADD : (opcode == OP_SHLI) ? TM_SHL : TM_SHR;
      luaK_codeABCk(fs, OP_MMBINI, b, sc, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    else if (opcode >= OP_ADDK && opcode <= OP_IDIVK) {
      int b = GETARG_B(inst);
      int c = GETARG_C(inst);
      TMS tm = cast(TMS, (opcode - OP_ADDK) + TM_ADD);
      luaK_codeABCk(fs, OP_MMBINK, b, c, cast_int(tm), 0);
      luaK_fixline(fs, line);
    }
    
    /* 更新 freereg */
    if (testAMode(opcode)) {
      int a = GETARG_A(inst);
      if (a >= fs->freereg) {
        int needed = a + 1 - fs->freereg;
        luaK_checkstack(fs, needed);
        fs->freereg = cast_byte(a + 1);
      }
    }
    
    testnext(ls, ';');
  }
}


/*
** 命令声明语法处理
** 语法: command 命令名(参数列表) 代码块 end
** 等价于: function 命令名(参数列表) 代码块 end; _CMDS["命令名"] = true
** 
** 参数：
**   ls - 词法状态
**   line - 行号
*/
static void commandstat (LexState *ls, int line) {
  /* commandstat -> COMMAND funcname body */
  expdesc v, b;
  TString *cmdname;
  
  luaX_next(ls);  /* skip COMMAND */
  
  /* 先保存命令名（不消费 token） */
  check(ls, TK_NAME);
  cmdname = ls->t.seminfo.ts;
  
  /* 使用 singlevar 获取变量描述符（这会消费 NAME token） */
  singlevar(ls, &v);
  
  /* 检查是否为只读 */
  check_readonly(ls, &v);
  
  /* 解析函数体 */
  body(ls, &b, 0, line);
  
  /* 存储函数到变量 */
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);
  
  /* 将命令名注册到 _CMDS 表: _CMDS[命令名] = true */
  {
    FuncState *fs = ls->fs;
    expdesc cmds_table, key_exp, val_exp;
    
    /* 获取 _CMDS 全局表 */
    singlevaraux(fs, luaS_newliteral(ls->L, "_CMDS"), &cmds_table, 1);
    if (cmds_table.k == VVOID) {
      /* _CMDS 不存在，从 _ENV 获取 */
      expdesc env_key;
      singlevaraux(fs, ls->envn, &cmds_table, 1);
      codestring(&env_key, luaS_newliteral(ls->L, "_CMDS"));
      luaK_indexed(fs, &cmds_table, &env_key);
    }
    
    /* 设置 _CMDS[命令名] = true */
    luaK_exp2anyregup(fs, &cmds_table);
    codestring(&key_exp, cmdname);
    init_exp(&val_exp, VTRUE, 0);
    luaK_indexed(fs, &cmds_table, &key_exp);
    luaK_storevar(fs, &cmds_table, &val_exp);
  }
}


/*
** 关键字声明语法处理
** 语法: keyword 关键字名(参数列表) 代码块 end
** 等价于: function 关键字名(参数列表) 代码块 end; _KEYWORDS["关键字名"] = 关键字名
** 
** 参数：
**   ls - 词法状态
**   line - 行号
** 说明：
**   将函数引用存储到 _KEYWORDS 表，支持宏调用语法 $name(args)
*/
static void keywordstat (LexState *ls, int line) {
  /* keywordstat -> KEYWORD funcname body */
  expdesc v, b;
  TString *kwname;
  
  luaX_next(ls);  /* skip KEYWORD */
  
  /* 先保存关键字名（不消费 token） */
  check(ls, TK_NAME);
  kwname = ls->t.seminfo.ts;
  
  /* 使用 singlevar 获取变量描述符（这会消费 NAME token） */
  singlevar(ls, &v);
  
  /* 检查是否为只读 */
  check_readonly(ls, &v);
  
  /* 解析函数体 */
  body(ls, &b, 0, line);
  
  /* 存储函数到变量 */
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);
  
  /* 将函数引用注册到 _KEYWORDS 表: _KEYWORDS[关键字名] = 函数引用 */
  {
    FuncState *fs = ls->fs;
    expdesc keywords_table, key_exp, func_exp;
    
    /* 获取 _KEYWORDS 全局表 */
    singlevaraux(fs, luaS_newliteral(ls->L, "_KEYWORDS"), &keywords_table, 1);
    if (keywords_table.k == VVOID) {
      /* _KEYWORDS 不存在，从 _ENV 获取 */
      expdesc env_key;
      singlevaraux(fs, ls->envn, &keywords_table, 1);
      codestring(&env_key, luaS_newliteral(ls->L, "_KEYWORDS"));
      luaK_indexed(fs, &keywords_table, &env_key);
    }
    
    /* 重新获取函数变量的值 */
    singlevaraux(fs, kwname, &func_exp, 1);
    if (func_exp.k == VVOID) {
      /* 从 _ENV 获取 */
      expdesc env_key2;
      singlevaraux(fs, ls->envn, &func_exp, 1);
      codestring(&env_key2, kwname);
      luaK_indexed(fs, &func_exp, &env_key2);
    }
    luaK_exp2anyreg(fs, &func_exp);
    
    /* 设置 _KEYWORDS[关键字名] = 函数引用 */
    luaK_exp2anyregup(fs, &keywords_table);
    codestring(&key_exp, kwname);
    luaK_indexed(fs, &keywords_table, &key_exp);
    luaK_storevar(fs, &keywords_table, &func_exp);
  }
}


/*
** operatorstat - 解析 operator 语句
** 语法: operator <符号> (参数列表) 语句块 end
** 功能描述：
**   定义自定义运算符，将函数存储到 _OPERATORS 表中
** 参数：
**   ls - 词法状态
**   line - 行号
** 示例：
**   operator ++ (a) return a + 1 end
**   operator ** (a, b) return a ^ b end
*/
static void operatorstat (LexState *ls, int line) {
  /* operatorstat -> OPERATOR <符号> body */
  expdesc b;
  TString *opname = NULL;
  FuncState *fs = ls->fs;
  const char *opstr = NULL;
  
  luaX_next(ls);  /* 跳过 OPERATOR */
  
  /* 解析运算符符号 - 支持各种符号组合 */
  int tok = ls->t.token;
  
  /* 根据当前token类型获取运算符字符串 */
  switch (tok) {
    case TK_PLUSPLUS: opstr = "++"; break;
    case TK_CONCAT: opstr = ".."; break;
    case TK_IDIV: opstr = "//"; break;
    case TK_SHL: opstr = "<<"; break;
    case TK_SHR: opstr = ">>"; break;
    case TK_EQ: opstr = "=="; break;
    case TK_NE: opstr = "~="; break;
    case TK_LE: opstr = "<="; break;
    case TK_GE: opstr = ">="; break;
    case TK_PIPE: opstr = "|>"; break;
    case TK_REVPIPE: opstr = "<|"; break;
    case TK_SPACESHIP: opstr = "<=>"; break;
    case TK_NULLCOAL: opstr = "??"; break;
    case TK_ARROW: opstr = "->"; break;
    case TK_MEAN: opstr = "=>"; break;
    case TK_ADDEQ: opstr = "+="; break;
    case TK_SUBEQ: opstr = "-="; break;
    case TK_MULEQ: opstr = "*="; break;
    case TK_DIVEQ: opstr = "/="; break;
    case TK_MODEQ: opstr = "%="; break;
    case '+': opstr = "+"; break;
    case '-': opstr = "-"; break;
    case '*': opstr = "*"; break;
    case '/': opstr = "/"; break;
    case '%': opstr = "%"; break;
    case '^': opstr = "^"; break;
    case '#': opstr = "#"; break;
    case '&': opstr = "&"; break;
    case '|': opstr = "|"; break;
    case '~': opstr = "~"; break;
    case '<': opstr = "<"; break;
    case '>': opstr = ">"; break;
    case '@': opstr = "@"; break;
    case TK_NAME:
      /* 支持命名运算符如 __add, __sub 等 */
      opname = ls->t.seminfo.ts;
      break;
    case TK_STRING:
      /* 支持字符串形式的运算符如 "**" */
      opname = ls->t.seminfo.ts;
      break;
    default:
      luaX_syntaxerror(ls, "expected operator symbol after 'operator'");
  }
  
  /* 如果是固定字符串，创建 TString */
  if (opstr != NULL) {
    opname = luaS_new(ls->L, opstr);
  }
  
  luaX_next(ls);  /* 消费运算符符号 */
  
  /* 解析函数体 (参数列表和函数体) */
  body(ls, &b, 0, line);
  
  /* 将函数注册到 _OPERATORS 表: _OPERATORS[运算符] = 函数 */
  {
    expdesc operators_table, key_exp;
    
    /* 获取 _OPERATORS 全局表 */
    singlevaraux(fs, luaS_newliteral(ls->L, "_OPERATORS"), &operators_table, 1);
    if (operators_table.k == VVOID) {
      /* _OPERATORS 不存在，从 _ENV 获取 */
      expdesc env_key;
      singlevaraux(fs, ls->envn, &operators_table, 1);
      codestring(&env_key, luaS_newliteral(ls->L, "_OPERATORS"));
      luaK_indexed(fs, &operators_table, &env_key);
    }
    
    /* 确保函数在寄存器中 */
    luaK_exp2anyreg(fs, &b);
    
    /* 设置 _OPERATORS[运算符] = 函数 */
    luaK_exp2anyregup(fs, &operators_table);
    codestring(&key_exp, opname);
    luaK_indexed(fs, &operators_table, &key_exp);
    luaK_storevar(fs, &operators_table, &b);
  }
  
  luaK_fixline(fs, line);
}


static void funcstat (LexState *ls, int line) {
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  luaX_next(ls);  /* skip FUNCTION */
  ismethod = funcname(ls, &v);
  check_readonly(ls, &v);
  body(ls, &b, ismethod, line);

  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition "happens" in the first line */
}


/*
** =====================================================================
** 面向对象系统：类定义解析
** 语法格式:
**   class ClassName [extends ParentClass] [implements Interface1, Interface2]
**     [static] function methodName(self, args) ... end
**     propertyName = value
**   end
** =====================================================================
*/

/*
** 访问级别枚举
*/
#define ACCESS_PUBLIC    0
#define ACCESS_PROTECTED 1
#define ACCESS_PRIVATE   2

/*
** 解析类方法定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 function methodName(self, ...) ... end 形式的方法定义
*/
static void class_method(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc method_exp, key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 生成方法体 */
  /* ismethod=0: 用户需要手动在参数列表中写 self */
  body(ls, &method_exp, 0, line);
  
  /* 将方法存储到类表中 */
  int methods_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 根据静态性和访问级别选择存储表 */
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态方法存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__methods");  /* 公开方法 */
  }
  
  /* 生成: R[methods_reg] = R[class_reg][table_name] */
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置方法: methods[method_name] = method_func */
  codestring(&key_exp, method_name);
  luaK_exp2anyreg(fs, &method_exp);
  
  /* 使用SETFIELD指令设置方法 */
  int key_k = luaK_stringK(fs, method_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析类属性定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态属性
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 propertyName = value 形式的属性定义
*/
static void class_property(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, val_exp;
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 解析赋值 */
  checknext(ls, '=');
  expr(ls, &val_exp);
  luaK_exp2anyreg(fs, &val_exp);
  
  /* 设置属性到对应表 */
  int statics_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 根据访问级别和静态性选择存储表 */
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态属性存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 公开属性也存储到 __statics（类级别属性） */
  }
  
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, val_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析getter属性访问器
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 语法：
**   [private|protected|public] get propertyName(self) ... end
** 说明：
**   当访问指定属性时，会调用getter函数
**   支持访问控制修饰符
*/
static void class_getter(LexState *ls, int class_reg, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, method_exp;
  int line = ls->linenumber;
  
  /* 跳过 'get' 已在调用前处理 */
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 生成getter函数体 */
  body(ls, &method_exp, 0, line);
  
  /* 根据访问级别选择存储表 */
  const char *table_name;
  if (access_level == ACCESS_PRIVATE) {
    table_name = "__private_getters";
  } else if (access_level == ACCESS_PROTECTED) {
    table_name = "__protected_getters";
  } else {
    table_name = "__getters";  /* 公开 */
  }
  
  /* 将getter存储到对应的表中 */
  int getters_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *getters_ts = luaS_new(ls->L, table_name);
  init_exp(&key_exp, VK, luaK_stringK(fs, getters_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置: getters_table[prop_name] = getter_func */
  luaK_exp2anyreg(fs, &method_exp);
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析setter属性访问器
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 语法：
**   [private|protected|public] set propertyName(self, value) ... end
** 说明：
**   当设置指定属性时，会调用setter函数
**   支持访问控制修饰符
*/
static void class_setter(LexState *ls, int class_reg, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp, method_exp;
  int line = ls->linenumber;
  
  /* 跳过 'set' 已在调用前处理 */
  
  /* 获取属性名 */
  TString *prop_name = str_checkname(ls);
  
  /* 生成setter函数体 */
  body(ls, &method_exp, 0, line);
  
  /* 根据访问级别选择存储表 */
  const char *table_name;
  if (access_level == ACCESS_PRIVATE) {
    table_name = "__private_setters";
  } else if (access_level == ACCESS_PROTECTED) {
    table_name = "__protected_setters";
  } else {
    table_name = "__setters";  /* 公开 */
  }
  
  /* 将setter存储到对应的表中 */
  int setters_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *setters_ts = luaS_new(ls->L, table_name);
  init_exp(&key_exp, VK, luaK_stringK(fs, setters_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置: setters_table[prop_name] = setter_func */
  luaK_exp2anyreg(fs, &method_exp);
  int key_k = luaK_stringK(fs, prop_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  fs->freereg = class_reg + 1;
}


/*
** 解析抽象方法声明
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 abstract function methodName(params) 形式的抽象方法声明
**   抽象方法只有签名，没有方法体，子类必须实现
*/
static void class_abstract_method(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  expdesc key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 解析参数列表并计算参数个数 */
  checknext(ls, '(');
  int param_count = 0;
  while (ls->t.token != ')' && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_NAME) {
      param_count++;
    }
    luaX_next(ls);
  }
  checknext(ls, ')');
  
  /* 将抽象方法名添加到 __abstracts 表，值为参数个数 */
  int abstracts_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *abstracts_ts = luaS_newliteral(ls->L, "__abstracts");
  init_exp(&key_exp, VK, luaK_stringK(fs, abstracts_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置 abstracts[method_name] = param_count */
  int method_k = luaK_stringK(fs, method_name);
  luaK_codeABx(fs, OP_LOADI, fs->freereg, param_count);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, method_k, fs->freereg - 1);
  
  /* 同时标记类为抽象类 */
  /* 设置 __flags |= CLASS_FLAG_ABSTRACT */
  TString *flags_ts = luaS_newliteral(ls->L, "__flags");
  int flags_k = luaK_stringK(fs, flags_ts);
  expdesc class_exp2;
  init_exp(&class_exp2, VNONRELOC, class_reg);
  
  /* 获取当前 flags */
  int flags_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_GETFIELD, flags_reg, class_reg, flags_k);
  
  /* flags |= CLASS_FLAG_ABSTRACT (0x02) */
  luaK_codeABx(fs, OP_LOADI, fs->freereg, CLASS_FLAG_ABSTRACT);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_BOR, flags_reg, flags_reg, fs->freereg - 1);
  
  /* 写回 flags */
  luaK_codeABC(fs, OP_SETFIELD, class_reg, flags_k, flags_reg);
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析 final 方法定义
** 参数：
**   ls - 词法状态
**   class_reg - 类表所在的寄存器
**   is_static - 是否是静态方法
**   access_level - 访问级别（ACCESS_PUBLIC/PROTECTED/PRIVATE）
** 说明：
**   解析 final function methodName(params) ... end 形式的 final 方法
**   final 方法不可被子类重写
*/
static void class_final_method(LexState *ls, int class_reg, int is_static, int access_level) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc method_exp, key_exp;
  
  checknext(ls, TK_FUNCTION);
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  /* 生成方法体 */
  body(ls, &method_exp, 0, line);
  
  /* 将方法存储到对应表中 */
  int methods_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  TString *table_name_ts;
  if (is_static) {
    table_name_ts = luaS_newliteral(ls->L, "__statics");  /* 静态final方法存储到 __statics */
  } else if (access_level == ACCESS_PRIVATE) {
    table_name_ts = luaS_newliteral(ls->L, "__privates");
  } else if (access_level == ACCESS_PROTECTED) {
    table_name_ts = luaS_newliteral(ls->L, "__protected");
  } else {
    table_name_ts = luaS_newliteral(ls->L, "__methods");
  }
  
  init_exp(&key_exp, VK, luaK_stringK(fs, table_name_ts));
  expdesc class_exp;
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  codestring(&key_exp, method_name);
  luaK_exp2anyreg(fs, &method_exp);
  
  int key_k = luaK_stringK(fs, method_name);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, key_k, method_exp.u.info);
  
  /* 将方法名添加到 __finals 表，标记为不可重写 */
  TString *finals_ts = luaS_newliteral(ls->L, "__finals");
  init_exp(&key_exp, VK, luaK_stringK(fs, finals_ts));
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_indexed(fs, &class_exp, &key_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 设置 finals[method_name] = true */
  int method_k = luaK_stringK(fs, method_name);
  luaK_codeABC(fs, OP_LOADTRUE, fs->freereg, 0, 0);
  luaK_reserveregs(fs, 1);
  luaK_codeABC(fs, OP_SETFIELD, class_exp.u.info, method_k, fs->freereg - 1);
  
  fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
}


/*
** 解析类定义语句
** 参数：
**   ls - 词法状态
**   line - 起始行号
**   class_flags - 类修饰符标志（CLASS_FLAG_ABSTRACT、CLASS_FLAG_FINAL、CLASS_FLAG_SEALED）
** 语法：
**   [abstract|final|sealed] class ClassName [extends ParentClass] [implements Interface, ...]
**     成员定义...
**   end
*/
static void classstat(LexState *ls, int line, int class_flags, int isexport) {
  FuncState *fs = ls->fs;
  expdesc class_exp, parent_exp, v;
  TString *classname;
  int has_parent = 0;
  int class_reg;
  
  luaX_next(ls);  /* 跳过 'class' */
  
  /* 获取类名 */
  classname = str_checkname(ls);
  
  /* 创建类表 - 使用OP_NEWCLASS操作码 */
  class_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 生成 NEWCLASS 指令: R[class_reg] = newclass(K[Bx]) */
  int classname_k = luaK_stringK(fs, classname);
  luaK_codeABx(fs, OP_NEWCLASS, class_reg, classname_k);
  
  /* 如果有类修饰符（abstract、final、sealed），设置类标志 */
  if (class_flags != 0) {
    /* 获取 __flags 字段 */
    TString *flags_ts = luaS_newliteral(ls->L, "__flags");
    int flags_k = luaK_stringK(fs, flags_ts);
    int flags_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
    
    /* 读取当前 flags */
    luaK_codeABC(fs, OP_GETFIELD, flags_reg, class_reg, flags_k);
    
    /* flags |= class_flags */
    luaK_codeABx(fs, OP_LOADI, fs->freereg, class_flags);
    luaK_reserveregs(fs, 1);
    luaK_codeABC(fs, OP_BOR, flags_reg, flags_reg, fs->freereg - 1);
    
    /* 写回 flags */
    luaK_codeABC(fs, OP_SETFIELD, class_reg, flags_k, flags_reg);
    
    fs->freereg = class_reg + 1;  /* 释放临时寄存器 */
  }
  
  /* 检查是否有继承（软关键字 extends） */
  if (softkw_testnext(ls, SKW_EXTENDS, SOFTKW_CTX_CLASS_INHERIT)) {
    has_parent = 1;
    /* 解析父类表达式 */
    expr(ls, &parent_exp);
    luaK_exp2nextreg(fs, &parent_exp);
    
    /* 生成 INHERIT 指令: R[class_reg].__parent = R[parent_reg] */
    luaK_codeABC(fs, OP_INHERIT, class_reg, parent_exp.u.info, 0);
    fs->freereg--;  /* 释放父类寄存器 */
  }
  
  /* 检查是否实现接口（软关键字 implements） */
  if (softkw_testnext(ls, SKW_IMPLEMENTS, SOFTKW_CTX_CLASS_INHERIT)) {
    do {
      expdesc iface_exp;
      expr(ls, &iface_exp);
      luaK_exp2nextreg(fs, &iface_exp);
      /* 生成 OP_IMPLEMENT 指令: R[class_reg] implements R[iface_reg] */
      luaK_codeABC(fs, OP_IMPLEMENT, class_reg, iface_exp.u.info, 0);
      fs->freereg--;
    } while (testnext(ls, ','));
  }
  
  /* 解析类体 */
  while (!testnext(ls, TK_END)) {
    if (ls->t.token == TK_EOS) {
      luaX_syntaxerror(ls, "期望 'end' 来结束类定义");
      break;
    }
    
    /* 解析修饰符（支持任意顺序组合） */
    int access_level = ACCESS_PUBLIC;  /* 默认公开 */
    int is_static = 0;
    int is_abstract = 0;
    int is_final = 0;
    int has_access_modifier = 0;  /* 是否已经设置过访问修饰符 */
    
    /* 循环检查所有修饰符，支持任意顺序 */
    int found_modifier = 1;
    while (found_modifier) {
      found_modifier = 0;
      SoftKWID skw = softkw_check(ls, SOFTKW_CTX_CLASS_BODY);
      
      switch (skw) {
        case SKW_PRIVATE:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "不能指定多个访问修饰符");
          }
          access_level = ACCESS_PRIVATE;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);  /* 消费token */
          found_modifier = 1;
          break;
        case SKW_PROTECTED:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "不能指定多个访问修饰符");
          }
          access_level = ACCESS_PROTECTED;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_PUBLIC:
          if (has_access_modifier) {
            luaX_syntaxerror(ls, "不能指定多个访问修饰符");
          }
          access_level = ACCESS_PUBLIC;
          has_access_modifier = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_STATIC:
          if (is_static) {
            luaX_syntaxerror(ls, "重复的 static 修饰符");
          }
          is_static = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_ABSTRACT:
          if (is_abstract) {
            luaX_syntaxerror(ls, "重复的 abstract 修饰符");
          }
          is_abstract = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        case SKW_FINAL:
          if (is_final) {
            luaX_syntaxerror(ls, "重复的 final 修饰符");
          }
          is_final = 1;
          softkw_checknext(ls, SOFTKW_CTX_CLASS_BODY);
          found_modifier = 1;
          break;
        default:
          break;
      }
    }
    
    /* abstract 和 final 互斥 */
    if (is_abstract && is_final) {
      luaX_syntaxerror(ls, "方法不能同时是 abstract 和 final");
    }
    
    /* static 和 abstract 互斥（静态方法不能被重写，因此不能是抽象的） */
    if (is_static && is_abstract) {
      luaX_syntaxerror(ls, "静态方法不能是 abstract");
    }
    
    /* 检查是否是 getter/setter */
    if (softkw_testnext(ls, SKW_GET, SOFTKW_CTX_CLASS_BODY)) {
      /* getter 属性访问器 */
      class_getter(ls, class_reg, access_level);
      continue;
    }
    else if (softkw_testnext(ls, SKW_SET, SOFTKW_CTX_CLASS_BODY)) {
      /* setter 属性访问器 */
      class_setter(ls, class_reg, access_level);
      continue;
    }
    
    /* 解析成员 */
    if (is_abstract && ls->t.token == TK_FUNCTION) {
      /* 抽象方法声明 */
      class_abstract_method(ls, class_reg, is_static, access_level);
    }
    else if (is_final && ls->t.token == TK_FUNCTION) {
      /* final 方法定义 */
      class_final_method(ls, class_reg, is_static, access_level);
    }
    else if (ls->t.token == TK_FUNCTION) {
      /* 普通方法定义 */
      class_method(ls, class_reg, is_static, access_level);
    }
    else if (ls->t.token == TK_NAME) {
      /* 属性定义 */
      class_property(ls, class_reg, is_static, access_level);
    }
    else if (ls->t.token == ';') {
      /* 空语句，跳过 */
      luaX_next(ls);
    }
    else if (ls->t.token == TK_END) {
      /* 类体结束，不报错 */
      break;
    }
    else {
      luaX_syntaxerror(ls, "类体中的非法成员定义");
    }
  }
  
  /* 将类存储到变量中 */
  /* 检查是在全局还是局部作用域 */
  if (isexport) {
     new_localvar(ls, classname);
     add_export(ls, classname);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, classname, &v);
  }
  init_exp(&class_exp, VNONRELOC, class_reg);
  luaK_storevar(fs, &v, &class_exp);
  
  luaK_fixline(fs, line);
}


/*
** 解析接口定义语句
** 参数：
**   ls - 词法状态
**   line - 起始行号
** 语法：
**   interface InterfaceName
**     function methodName(self, ...)
**   end
*/
static void interfacestat(LexState *ls, int line) {
  FuncState *fs = ls->fs;
  expdesc iface_exp, v;
  TString *ifacename;
  int iface_reg;
  
  luaX_next(ls);  /* 跳过 'ointerface' */
  
  /* 获取接口名 */
  ifacename = str_checkname(ls);
  
  /* 创建接口表 */
  iface_reg = fs->freereg;
  luaK_reserveregs(fs, 1);
  
  /* 使用 NEWCLASS 创建接口（带有接口标志） */
  int ifacename_k = luaK_stringK(fs, ifacename);
  luaK_codeABx(fs, OP_NEWCLASS, iface_reg, ifacename_k);
  
  /* 设置接口标志 */
  luaK_codeABC(fs, OP_SETIFACEFLAG, iface_reg, 0, 0);
  
  /* 解析接口体 - 只允许方法声明 */
  while (!testnext(ls, TK_END)) {
    if (ls->t.token == TK_EOS) {
      luaX_syntaxerror(ls, "期望 'end' 来结束接口定义");
      break;
    }
    
    if (testnext(ls, TK_FUNCTION)) {
      /* 方法声明（只有签名，没有实现） */
      TString *method_name = str_checkname(ls);
      checknext(ls, '(');
      /* 解析参数列表并计算参数个数 */
      int param_count = 0;
      while (ls->t.token != ')' && ls->t.token != TK_EOS) {
        if (ls->t.token == TK_NAME) {
          param_count++;
        }
        luaX_next(ls);
      }
      checknext(ls, ')');
      
      /* 记录方法签名到接口表，值为参数个数 */
      int method_k = luaK_stringK(fs, method_name);
      luaK_codeABC(fs, OP_ADDMETHOD, iface_reg, method_k, param_count);
    }
    else if (ls->t.token == ';') {
      luaX_next(ls);
    }
    else {
      luaX_syntaxerror(ls, "接口中只能声明方法");
    }
  }
  
  /* 将接口存储到变量中 */
  buildglobal(ls, ifacename, &v);
  init_exp(&iface_exp, VNONRELOC, iface_reg);
  luaK_storevar(fs, &v, &iface_exp);
  
  luaK_fixline(fs, line);
}


/*
** 解析枚举定义
** 参数：
**   ls - 词法状态
**   line - enum 关键字所在行号
** 语法：
**   enum EnumName
**       Name [= value]
**       ...
**   end
** 
**   或大括号语法：
**   enum EnumName {
**       Name [= value],
**       ...
**   }
** 
** 枚举会被编译为一个表，其中枚举成员作为键，值为整数
** 如果没有显式赋值，则从0开始自动递增
*/
static void enumstat(LexState *ls, int line, int isexport) {
  FuncState *fs = ls->fs;
  expdesc enum_exp, v;
  TString *enumname;
  int enum_reg;
  int use_brace = 0;
  lua_Integer auto_value = 0;  /* 自动递增的枚举值，从0开始 */
  int nh = 0;  /* 枚举成员数量 */
  
  luaX_next(ls);  /* 跳过 'enum' */
  
  /* 获取枚举名 */
  enumname = str_checkname(ls);
  
  /* 检查是否使用大括号语法 */
  if (ls->t.token == '{') {
    use_brace = 1;
    luaX_next(ls);  /* 跳过 '{' */
  }
  
  /* 创建枚举表 */
  enum_reg = fs->freereg;
  int pc = luaK_codeABC(fs, OP_NEWTABLE, enum_reg, 0, 0);
  luaK_code(fs, 0);  /* 为额外参数预留空间 */
  luaK_reserveregs(fs, 1);
  
  /* 解析枚举成员 */
  for (;;) {
    /* 检查结束条件 */
    if (use_brace) {
      if (ls->t.token == '}') break;
    } else {
      if (ls->t.token == TK_END) break;
    }
    
    if (ls->t.token == TK_EOS) {
      if (use_brace) {
        luaX_syntaxerror(ls, "期望 '}' 来结束枚举定义");
      } else {
        luaX_syntaxerror(ls, "期望 'end' 来结束枚举定义");
      }
      break;
    }
    
    /* 跳过空语句 */
    if (ls->t.token == ';' || ls->t.token == ',') {
      luaX_next(ls);
      continue;
    }
    
    /* 解析枚举成员名 */
    if (ls->t.token != TK_NAME) {
      luaX_syntaxerror(ls, "期望枚举成员名称");
      break;
    }
    
    TString *member_name = str_checkname(ls);
    expdesc key, val;
    
    /* 设置键为成员名 */
    codestring(&key, member_name);
    
    /* 检查是否有显式赋值 */
    if (testnext(ls, '=')) {
      /* 显式赋值 */
      expdesc value_exp;
      expr(ls, &value_exp);
      
      /* 尝试获取常量值用于自动递增 */
      if (value_exp.k == VKINT) {
        auto_value = value_exp.u.ival + 1;
      } else if (value_exp.k == VKFLT) {
        auto_value = (lua_Integer)value_exp.u.nval + 1;
      } else {
        /* 非常量表达式，无法确定下一个自动值 */
        auto_value++;
      }
      
      /* 将值放入表中 */
      expdesc tab;
      init_exp(&tab, VNONRELOC, enum_reg);
      luaK_indexed(fs, &tab, &key);
      luaK_storevar(fs, &tab, &value_exp);
    } else {
      /* 自动赋值 */
      init_exp(&val, VKINT, 0);
      val.u.ival = auto_value++;
      
      /* 将值放入表中 */
      expdesc tab;
      init_exp(&tab, VNONRELOC, enum_reg);
      luaK_indexed(fs, &tab, &key);
      luaK_storevar(fs, &tab, &val);
    }
    
    nh++;
    
    /* 处理分隔符 */
    if (use_brace) {
      if (ls->t.token != '}') {
        testnext(ls, ',');  /* 可选的逗号 */
      }
    }
  }
  
  /* 跳过结束符 */
  if (use_brace) {
    checknext(ls, '}');
  } else {
    check_match(ls, TK_END, TK_ENUM, line);
  }
  
  /* 设置表大小 */
  luaK_settablesize(fs, pc, enum_reg, 0, nh);
  
  /* 将枚举表存储到全局变量中 */
  if (isexport) {
     new_localvar(ls, enumname);
     add_export(ls, enumname);
     adjustlocalvars(ls, 1);
     init_var(fs, &v, fs->nactvar - 1);
  } else {
     buildglobal(ls, enumname, &v);
  }
  init_exp(&enum_exp, VNONRELOC, enum_reg);
  luaK_storevar(fs, &v, &enum_exp);
  
  luaK_fixline(fs, line);
}


/*
** 解析 new 表达式
** 参数：
**   ls - 词法状态
**   v - 返回的表达式描述符
** 语法：
**   new ClassName(args...)
*/
static void newexpr(LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  expdesc class_exp, args_exp;
  
  luaX_next(ls);  /* 跳过 'onew' */
  
  /* 只解析主表达式（类名），不解析后面的函数调用 */
  primaryexp(ls, &class_exp);
  luaK_exp2nextreg(fs, &class_exp);
  
  /* 解析构造函数参数 */
  int nargs = 0;
  if (testnext(ls, '(')) {
    if (ls->t.token != ')') {
      do {
        expr(ls, &args_exp);
        luaK_exp2nextreg(fs, &args_exp);
        nargs++;
      } while (testnext(ls, ','));
    }
    checknext(ls, ')');
  }
  
  /* 生成 NEWOBJ 指令 */
  int result_reg = class_exp.u.info;
  luaK_codeABC(fs, OP_NEWOBJ, result_reg, class_exp.u.info, nargs + 1);
  
  init_exp(v, VNONRELOC, result_reg);
  fs->freereg = result_reg + 1;
}


/*
** 解析 super 表达式
** 参数：
**   ls - 词法状态
**   v - 返回的表达式描述符
** 语法：
**   super.methodName(args...)
**   super:methodName(args...)
*/
static void superexpr(LexState *ls, expdesc *v) {
  FuncState *fs = ls->fs;
  int line = ls->linenumber;
  
  luaX_next(ls);  /* 跳过 'osuper' */
  
  /* 查找当前作用域中的self变量 */
  expdesc self_exp;
  TString *self_name = luaS_newliteral(ls->L, "self");
  singlevaraux(fs, self_name, &self_exp, 1);
  
  if (self_exp.k == VVOID) {
    luaX_syntaxerror(ls, "super 只能在类方法中使用");
  }
  
  int is_method_call = 0;
  if (ls->t.token == ':') {
    is_method_call = 1;
    luaX_next(ls);  /* 跳过 ':' */
  }
  else if (ls->t.token == '.') {
    luaX_next(ls);  /* 跳过 '.' */
  }
  else {
    luaX_syntaxerror(ls, "super 后期望 '.' 或 ':'");
  }
  
  /* 获取方法名 */
  TString *method_name = str_checkname(ls);
  
  if (is_method_call) {
    /*
    ** super:method(args) - 方法调用语法
    ** 需要直接处理完整的调用，避免 suffixedexp 重新分配寄存器
    */
    luaK_exp2anyreg(fs, &self_exp);
    int self_reg = self_exp.u.info;
    
    /* 分配连续寄存器用于调用: [method, self, arg1, arg2, ...] */
    int base_reg = fs->freereg;
    luaK_reserveregs(fs, 2);  /* 为 method 和 self 预留 */
    
    /* 生成 GETSUPER: base_reg = 父类方法 */
    int method_k = luaK_stringK(fs, method_name);
    luaK_codeABC(fs, OP_GETSUPER, base_reg, self_reg, method_k);
    
    /* base_reg + 1 = self */
    luaK_codeABC(fs, OP_MOVE, base_reg + 1, self_reg, 0);
    
    /* 现在处理参数列表 */
    if (ls->t.token == '(') {
      expdesc args;
      int nparams;
      luaX_next(ls);  /* 跳过 '(' */
      if (ls->t.token == ')') {
        args.k = VVOID;
      } else {
        explist(ls, &args);
        if (hasmultret(args.k))
          luaK_setmultret(fs, &args);
      }
      check_match(ls, ')', '(', line);
      
      if (hasmultret(args.k))
        nparams = LUA_MULTRET;
      else {
        if (args.k != VVOID)
          luaK_exp2nextreg(fs, &args);
        nparams = fs->freereg - (base_reg + 1);  /* self 也是参数 */
      }
      
      /* 生成 CALL 指令 */
      init_exp(v, VCALL, luaK_codeABC(fs, OP_CALL, base_reg, nparams + 1, 2));
      luaK_fixline(fs, line);
      fs->freereg = base_reg + 1;  /* 调用后只留一个返回值 */
    } else {
      luaX_syntaxerror(ls, "super:method 后期望 '('");
    }
  }
  else {
    /*
    ** super.method - 只获取父类方法，不绑定 self
    */
    luaK_exp2anyreg(fs, &self_exp);
    int method_k = luaK_stringK(fs, method_name);
    int result_reg = fs->freereg;
    luaK_reserveregs(fs, 1);
    luaK_codeABC(fs, OP_GETSUPER, result_reg, self_exp.u.info, method_k);
    
    init_exp(v, VNONRELOC, result_reg);
  }
}


/*
** 检查token是否是复合赋值运算符
** 参数：
**   token - 要检查的token
** 返回值：
**   对应的二元运算符类型，如果不是复合赋值运算符则返回OPR_NOBINOPR
*/
static BinOpr getcompoundop (int token) {
  switch (token) {
    case TK_ADDEQ:    return OPR_ADD;     /* += */
    case TK_SUBEQ:    return OPR_SUB;     /* -= */
    case TK_MULEQ:    return OPR_MUL;     /* *= */
    case TK_DIVEQ:    return OPR_DIV;     /* /= */
    case TK_IDIVEQ:   return OPR_IDIV;    /* //= */
    case TK_MODEQ:    return OPR_MOD;     /* %= */
    case TK_BANDEQ:   return OPR_BAND;    /* &= */
    case TK_BOREQ:    return OPR_BOR;     /* |= */
    case TK_BXOREQ:   return OPR_BXOR;    /* ~= 作为位异或赋值 */
    case TK_SHREQ:    return OPR_SHR;     /* >>= */
    case TK_SHLEQ:    return OPR_SHL;     /* <<= */
    case TK_CONCATEQ: return OPR_CONCAT;  /* ..= */
    case TK_NE:       return OPR_BXOR;    /* ~= 在赋值上下文中作为位异或赋值 */
    default:          return OPR_NOBINOPR;
  }
}


/*
** 处理复合赋值运算符
** 语法: var op= expr  =>  var = var op expr
** 参数：
**   ls - 词法状态
**   var - 左侧变量的表达式描述符
**   opr - 二元运算符类型
*/
static void compoundassign (LexState *ls, expdesc *var, BinOpr opr) {
  FuncState *fs = ls->fs;
  expdesc e1, e2;
  int line = ls->linenumber;
  
  /* 检查变量是否可赋值 */
  check_condition(ls, vkisvar(var->k), "syntax error");
  check_readonly(ls, var);
  
  /* 跳过复合赋值运算符 */
  luaX_next(ls);
  
  /* 复制变量表达式用于读取当前值 */
  e1 = *var;
  
  /* 将变量转换为寄存器（读取当前值） */
  luaK_exp2nextreg(fs, &e1);
  
  /* 读取右侧表达式 */
  expr(ls, &e2);
  
  /* 准备二元运算 */
  luaK_infix(fs, opr, &e1);
  
  /* 执行二元运算 */
  luaK_posfix(fs, opr, &e1, &e2, line);
  
  /* 将结果转换为任意寄存器 */
  luaK_exp2anyreg(fs, &e1);
  
  /* 将结果存储回变量 */
  luaK_storevar(fs, var, &e1);
}


/*
** 处理自增运算符 (a++)
** 语法: var++  =>  var = var + 1
** 参数：
**   ls - 词法状态
**   var - 变量的表达式描述符
*/
static void incrementstat (LexState *ls, expdesc *var) {
  FuncState *fs = ls->fs;
  expdesc e1, e2;
  int line = ls->linenumber;
  
  /* 检查变量是否可赋值 */
  check_condition(ls, vkisvar(var->k), "syntax error");
  check_readonly(ls, var);
  
  /* 跳过 ++ 运算符 */
  luaX_next(ls);
  
  /* 复制变量表达式用于读取当前值 */
  e1 = *var;
  
  /* 将变量转换为寄存器（读取当前值） */
  luaK_exp2nextreg(fs, &e1);
  
  /* 创建常量1 */
  init_exp(&e2, VKINT, 0);
  e2.u.ival = 1;
  
  /* 准备加法运算 */
  luaK_infix(fs, OPR_ADD, &e1);
  
  /* 执行加法运算 */
  luaK_posfix(fs, OPR_ADD, &e1, &e2, line);
  
  /* 将结果转换为任意寄存器 */
  luaK_exp2anyreg(fs, &e1);
  
  /* 将结果存储回变量 */
  luaK_storevar(fs, var, &e1);
}


/* 前向声明：Shell 风格命令调用 */
static int try_command_call (LexState *ls);

static void exprstat (LexState *ls) {
  /* stat -> func | assignment | compoundassign | increment | cmdcall */
  FuncState *fs = ls->fs;
  struct LHS_assign v;
  
  /* 优先尝试 Shell 风格命令调用 */
  if (try_command_call(ls)) {
    return;
  }
  
  suffixedexp(ls, &v.v);
  
  /* 检查是否是自增运算符 */
  if (ls->t.token == TK_PLUSPLUS) {
    incrementstat(ls, &v.v);
    return;
  }
  
  /* 检查是否是复合赋值运算符 */
  BinOpr opr = getcompoundop(ls->t.token);
  if (opr != OPR_NOBINOPR) {
    compoundassign(ls, &v.v, opr);
    return;
  }
  
  if (ls->t.token == '=' || ls->t.token == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    restassign(ls, &v, 1);
  }
  else {  /* stat -> func */
    Instruction *inst;
    check_condition(ls, v.v.k == VCALL, "syntax error");
    inst = &getinstruction(fs, &v.v);
    SETARG_C(*inst, 1);  /* call statement uses no results */
  }
}


/*
** 检查当前 token 是否可以作为命令参数的开始
** 参数：
**   token - 要检查的 token
** 返回值：
**   1 如果可以作为参数开始，0 否则
*/
static int is_cmd_arg_start (int token) {
  switch (token) {
    case TK_STRING:
    case TK_INTERPSTRING:
    case TK_RAWSTRING:
    case TK_INT:
    case TK_FLT:
    case TK_NAME:
    case TK_TRUE:
    case TK_FALSE:
    case TK_NIL:
    case '{':
    case '(':
    case '-':  /* 可能是负数或操作符 */
      return 1;
    default:
      return 0;
  }
}


/*
** 检查当前 token 是否是语句结束符
** 参数：
**   token - 要检查的 token
** 返回值：
**   1 如果是语句结束符，0 否则
*/
static int is_stmt_terminator (int token) {
  switch (token) {
    case ';':
    case TK_EOS:
    case TK_END:
    case TK_THEN:
    case TK_ELSE:
    case TK_ELSEIF:
    case TK_UNTIL:
    case TK_DO:
    case TK_RETURN:
    case TK_BREAK:
    case TK_CONTINUE:
      return 1;
    default:
      return 0;
  }
}


/*
** Shell 风格命令调用语法处理
** 语法: 命令名 参数1 参数2 ...
** 等价于: 命令名(参数1, 参数2, ...)
** 
** 参数：
**   ls - 词法状态
** 返回值：
**   1 如果成功解析为命令调用，0 否则
*/
static int try_command_call (LexState *ls) {
  FuncState *fs = ls->fs;
  
  /* 检查是否是 TK_NAME 后面跟着参数 */
  if (ls->t.token != TK_NAME) {
    return 0;
  }
  
  /* 预读下一个 token，判断是否可能是命令调用 */
  int lookahead = luaX_lookahead(ls);
  
  /* 如果是普通函数调用/方法调用/字段访问/赋值，不处理 */
  if (lookahead == '(' || lookahead == ':' || lookahead == '.' ||
      lookahead == '=' || lookahead == ',' || lookahead == '[' ||
      lookahead == TK_PLUSPLUS || getcompoundop(lookahead) != OPR_NOBINOPR) {
    return 0;
  }
  
  /* 如果下一个 token 不能作为命令参数开始，不处理 */
  if (!is_cmd_arg_start(lookahead)) {
    return 0;
  }
  
  /*
  ** 重要：Lua 原生支持 func "string" 和 func {table} 语法（单参数调用）
  ** 这种情况会在 suffixedexp 中正确处理链式调用（如 .method()）
  ** 只有当检测到多个参数时，才使用命令调用模式
  ** 
  ** 判断逻辑：如果第一个参数是字符串或表，且后面紧跟 '.' 或 ':'
  ** 或者后面没有更多参数，就让 suffixedexp 处理
  */
  if (lookahead == TK_STRING || lookahead == TK_INTERPSTRING || lookahead == TK_RAWSTRING || lookahead == '{') {
    /* 这是 Lua 原生支持的单参数调用语法，让 suffixedexp 处理 */
    /* 它会正确处理后续的链式调用 */
    return 0;
  }
  
  /* 解析命令调用 */
  int line = ls->linenumber;
  expdesc func;
  int base;
  int nargs = 0;
  
  /* 获取命令名 */
  TString *cmdname = ls->t.seminfo.ts;
  
  /* 首先检查 _CMDS[命令名] 是否存在，生成运行时检查代码 */
  /* 获取命令函数 */
  singlevar(ls, &func);
  luaK_exp2nextreg(fs, &func);
  base = func.u.info;
  
  /* 解析参数列表（只在同一行内解析，换行即停止） */
  while (!is_stmt_terminator(ls->t.token) && ls->t.token != TK_EOS && ls->linenumber == line) {
    expdesc arg;
    
    /* 检查是否遇到语句结束 */
    if (is_stmt_terminator(ls->t.token)) {
      break;
    }
    
    /* 处理特殊的操作符参数（如 -f, -r 等） */
    if (ls->t.token == '-') {
      int next = luaX_lookahead(ls);
      if (next == TK_NAME) {
        /* 构造 "-xxx" 字符串 */
        luaX_next(ls);  /* 跳过 '-' */
        TString *op_name = ls->t.seminfo.ts;
        const char *name = getstr(op_name);
        size_t len = tsslen(op_name);
        char *buf = luaM_newvector(ls->L, len + 2, char);
        buf[0] = '-';
        memcpy(buf + 1, name, len);
        buf[len + 1] = '\0';
        TString *op_str = luaS_newlstr(ls->L, buf, len + 1);
        luaM_freearray(ls->L, buf, len + 2);
        codestring(&arg, op_str);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        continue;
      } else if (next == TK_INT || next == TK_FLT) {
        /* 负数 */
        luaX_next(ls);  /* 跳过 '-' */
        if (ls->t.token == TK_INT) {
          init_exp(&arg, VKINT, 0);
          arg.u.ival = -ls->t.seminfo.i;
        } else {
          init_exp(&arg, VKFLT, 0);
          arg.u.nval = -ls->t.seminfo.r;
        }
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        continue;
      }
    }
    
    /* 处理普通参数 */
    switch (ls->t.token) {
      case TK_STRING:
      case TK_INTERPSTRING:
      case TK_RAWSTRING: {
        codestring(&arg, ls->t.seminfo.ts);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_INT: {
        init_exp(&arg, VKINT, 0);
        arg.u.ival = ls->t.seminfo.i;
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_FLT: {
        init_exp(&arg, VKFLT, 0);
        arg.u.nval = ls->t.seminfo.r;
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_TRUE: {
        init_exp(&arg, VTRUE, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_FALSE: {
        init_exp(&arg, VFALSE, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_NIL: {
        init_exp(&arg, VNIL, 0);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        luaX_next(ls);
        break;
      }
      case TK_NAME: {
        /* 变量引用 */
        singlevar(ls, &arg);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      case '{': {
        /* 表构造器 */
        constructor(ls, &arg);
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      case '(': {
        /* 括号表达式 */
        luaX_next(ls);
        expr(ls, &arg);
        checknext(ls, ')');
        luaK_exp2nextreg(fs, &arg);
        nargs++;
        break;
      }
      default: {
        /* 不是有效参数，停止解析 */
        goto done_args;
      }
    }
  }
  
done_args:
  /* 生成函数调用指令，C=1表示不需要返回值（C=0是multret会破坏栈状态） */
  init_exp(&func, VCALL, luaK_codeABC(fs, OP_CALL, base, nargs + 1, 1));
  luaK_fixline(fs, line);
  fs->freereg = base;
  
  return 1;
}


static void retstat (LexState *ls) {
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->fs;
  expdesc e;
  int nret;  /* number of values being returned */
  int first = luaY_nvarstack(fs);  /* first slot to be returned */
  if (block_follow(ls, 1) || ls->t.token == ';')
    nret = 0;  /* return no values */
  else {
    nret = explist(ls, &e);  /* optional return values */
    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1 && !fs->bl->insidetbc) {  /* tail call? */
        SET_OPCODE(getinstruction(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getinstruction(fs,&e)) == luaY_nvarstack(fs));
      }
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = luaK_exp2anyreg(fs, &e);  /* can use original slot */
      else {  /* values must go to the top of the stack */
        luaK_exp2nextreg(fs, &e);
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
  testnext(ls, ';');  /* skip optional semicolon */
}


static int is_preprocessor_directive(const char *name) {
  return strcmp(name, "include") == 0 ||
         strcmp(name, "alias") == 0 ||
         strcmp(name, "define") == 0 ||
         strcmp(name, "if") == 0 ||
         strcmp(name, "else") == 0 ||
         strcmp(name, "elseif") == 0 ||
         strcmp(name, "end") == 0 ||
         strcmp(name, "haltcompiler") == 0;
}

static void parse_alias(LexState *ls) {
  TString *name = str_checkname(ls);
  checknext(ls, '=');

  int capacity = 8;
  int n = 0;
  Token *tokens = luaM_newvector(ls->L, capacity, Token);
  int line = ls->linenumber;

  while (ls->linenumber == line && ls->t.token != TK_EOS) {
     if (n >= capacity) {
       int oldcap = capacity;
       capacity *= 2;
       tokens = luaM_reallocvector(ls->L, tokens, oldcap, capacity, Token);
     }
     tokens[n++] = ls->t;
     luaX_next(ls);
  }

  luaX_addalias(ls, name, tokens, n);
}

static int eval_const_condition(LexState *ls) {
  int val = 0;
  /* Simple evaluation: literals */
  if (ls->t.token == TK_TRUE) val = 1;
  else if (ls->t.token == TK_FALSE) val = 0;
  else if (ls->t.token == TK_INT) val = (ls->t.seminfo.i != 0);
  else if (ls->t.token == TK_NAME) {
     if (ls->defines) {
        TValue key;
        setsvalue(ls->L, &key, ls->t.seminfo.ts);
        const TValue *v = luaH_get(ls->defines, &key);
        val = !l_isfalse(v);
     } else {
        val = 0;
     }
  }
  else {
     /* luaX_syntaxerror(ls, "invalid condition in $if"); */
     val = 0;
  }
  luaX_next(ls); /* consume value */

  /* consume trailing tokens in condition if any (e.g. 'then' if user typed it, though pluto uses none or 'then') */
  if (ls->t.token == TK_THEN) luaX_next(ls);

  return val;
}

static void constexprdefinestat (LexState *ls) {
  luaX_next(ls); /* skip 'define' */
  TString *name = str_checkname(ls);
  checknext(ls, '=');

  expdesc e;
  expr(ls, &e);

  TValue k;
  if (!luaK_exp2const(ls->fs, &e, &k)) {
     luaX_syntaxerror(ls, "variable was not assigned a compile-time constant value");
  }

  if (ls->defines == NULL) {
     ls->defines = luaH_new(ls->L);
     /* anchor defines table to prevent GC */
     sethvalue2s(ls->L, ls->L->top.p, ls->defines);
     ls->L->top.p++;
  }

  TValue key;
  setsvalue(ls->L, &key, name);
  luaH_set(ls->L, ls->defines, &key, &k);
}

static void skip_block(LexState *ls) {
  int depth = 1;
  while (depth > 0 && ls->t.token != TK_EOS) {
    if (ls->t.token == TK_DOLLAR) {
       int la = luaX_lookahead(ls);
       if (la == TK_NAME) {
         const char *name = getstr(ls->lookahead.seminfo.ts);
         if (strcmp(name, "if") == 0) {
           depth++;
         }
         else if (strcmp(name, "end") == 0) {
           depth--;
           if (depth == 0) return; /* Don't consume $end yet */
         }
         else if (strcmp(name, "else") == 0 || strcmp(name, "elseif") == 0) {
           if (depth == 1) {
             return; /* Stop at else/elseif of current block */
           }
         }
       } else if (la == TK_IF) {
         depth++;
       } else if (la == TK_END) {
         depth--;
         if (depth == 0) return;
       } else if (la == TK_ELSE || la == TK_ELSEIF) {
         if (depth == 1) return;
       }
    }
    luaX_next(ls);
  }
}

static void consume_end_tag(LexState *ls) {
  if (ls->t.token == TK_DOLLAR) {
    luaX_next(ls);
    if (ls->t.token == TK_END) {
      luaX_next(ls);
    } else if (ls->t.token == TK_NAME && strcmp(getstr(ls->t.seminfo.ts), "end") == 0) {
      luaX_next(ls);
    }
  }
}

static void constexprifstat(LexState *ls) {
   int cond = eval_const_condition(ls);

   if (cond) {
      statlist(ls);
   } else {
      skip_block(ls);
   }

   if (ls->t.token == TK_DOLLAR) {
      luaX_next(ls); /* skip $ */
      int is_else = 0;
      int is_elseif = 0;
      int is_end = 0;

      if (ls->t.token == TK_ELSE) is_else = 1;
      else if (ls->t.token == TK_ELSEIF) is_elseif = 1;
      else if (ls->t.token == TK_END) is_end = 1;
      else if (ls->t.token == TK_NAME) {
         const char *name = getstr(ls->t.seminfo.ts);
         if (strcmp(name, "else") == 0) is_else = 1;
         else if (strcmp(name, "elseif") == 0) is_elseif = 1;
         else if (strcmp(name, "end") == 0) is_end = 1;
      }

      if (is_else) {
         luaX_next(ls);
         if (cond) {
            skip_block(ls);
            consume_end_tag(ls);
         } else {
            statlist(ls);
            consume_end_tag(ls);
         }
      } else if (is_elseif) {
         luaX_next(ls);
         if (cond) {
            /* We took the if branch, so skip everything until end */
            int depth = 1;
            while (depth > 0 && ls->t.token != TK_EOS) {
               if (ls->t.token == TK_DOLLAR) {
                  int la = luaX_lookahead(ls);
                  if (la == TK_NAME) {
                     const char *n = getstr(ls->lookahead.seminfo.ts);
                     if (strcmp(n, "if") == 0) depth++;
                     else if (strcmp(n, "end") == 0) {
                        depth--;
                        if (depth == 0) break;
                     }
                  } else if (la == TK_IF) depth++;
                  else if (la == TK_END) {
                     depth--;
                     if (depth == 0) break;
                  }
               }
               luaX_next(ls);
            }
            consume_end_tag(ls);
         } else {
            constexprifstat(ls);
         }
      } else if (is_end) {
         luaX_next(ls);
      }
   }
}

static void constexprstat (LexState *ls) {
  luaX_next(ls); /* skip $ */

  if (ls->t.token == TK_IF) {
     luaX_next(ls);
     constexprifstat(ls);
     return;
  }

  /* Fallback for other directives that are names */
  if (ls->t.token != TK_NAME) {
     /* Should not happen if statement() checked correctly, but for safety */
     return;
  }

  TString *ts = ls->t.seminfo.ts;
  const char *name = getstr(ts);

  if (strcmp(name, "include") == 0) {
     luaX_next(ls);
     if (ls->t.token != TK_STRING && ls->t.token != TK_RAWSTRING) {
       luaX_syntaxerror(ls, "expected filename string after $include");
     }
     luaX_pushincludefile(ls, getstr(ls->t.seminfo.ts));
     luaX_next(ls);
  }
  else if (strcmp(name, "alias") == 0) {
     luaX_next(ls);
     parse_alias(ls);
  }
  else if (strcmp(name, "haltcompiler") == 0) {
     while (ls->t.token != TK_EOS) luaX_next(ls);
  }
  else if (strcmp(name, "if") == 0) {
     luaX_next(ls);
     constexprifstat(ls);
  }
  else if (strcmp(name, "define") == 0) {
     constexprdefinestat(ls);
  }
  else {
     /* unknown directive - ignore line */
     luaX_next(ls);
     int line = ls->linenumber;
     while (ls->linenumber == line && ls->t.token != TK_EOS) luaX_next(ls);
  }
}

static void statement (LexState *ls) {
  int line = ls->linenumber;  /* may be needed for error messages */
  enterlevel(ls);
  switch (ls->t.token) {
    case ';': {  /* stat -> ';' (empty statement) */
      luaX_next(ls);  /* skip ';' */
      break;
    }
    case TK_WHEN: {  /* stat -> ifstat */
      whenstat(ls, line);
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      break;
    }
    case TK_DOLLAR: { /* stat -> constexprstat or macro */
      int la = luaX_lookahead(ls);
      if (la == TK_NAME) {
         TString *ts = ls->lookahead.seminfo.ts;
         const char *name = getstr(ts);
         if (is_preprocessor_directive(name)) {
            constexprstat(ls);
            break;
         }
      } else if (la == TK_IF || la == TK_ELSE || la == TK_ELSEIF || la == TK_END) {
         constexprstat(ls);
         break;
      }
      /* Fallthrough to exprstat */
      exprstat(ls);
      break;
    }
    case TK_SWITCH:{
      switchstat(ls, line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      luaX_next(ls);  /* skip DO */
      block(ls);
      check_match(ls, TK_END, TK_DO, line);
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      break;
    }
    case TK_TRY: {  /* stat -> trystat */
      trystat(ls, line);
      break;
    }
    case TK_WITH: {  /* stat -> withstat */
      withstat(ls, line);
      break;
    }
    case TK_ASM: {  /* stat -> asmstat */
      asmstat(ls, line);
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      funcstat(ls, line);
      break;
    }
    case TK_ENUM: {  /* stat -> enumstat */
      enumstat(ls, line, 0);
      break;
    }
    case TK_EXPORT: {
      luaX_next(ls);
      if (testnext(ls, TK_FUNCTION)) {
        localfunc(ls, 1);
      }
      else if (testnext(ls, TK_LOCAL)) {
        localstat(ls, 1);
      }
      else if (ls->t.token == TK_ENUM) {
        enumstat(ls, line, 1);
      }
      else if (testnext(ls, TK_CONST)) {
        if (testnext(ls, TK_FUNCTION))
          luaK_semerror(ls, "function cannot be declared as const");
        else
          localstat(ls, 1);
      }
      else {
        SoftKWID skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (skw == SKW_CLASS) {
          classstat(ls, line, 0, 1);
        }
        else if (skw == SKW_ABSTRACT) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_ABSTRACT, 1);
          else
             luaX_syntaxerror(ls, "'abstract' export must be followed by 'class'");
        }
        else if (skw == SKW_FINAL) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_FINAL, 1);
          else
             luaX_syntaxerror(ls, "'final' export must be followed by 'class'");
        }
        else if (skw == SKW_SEALED) {
          luaX_next(ls);
          if (softkw_check(ls, SOFTKW_CTX_STMT_BEGIN) == SKW_CLASS)
             classstat(ls, line, CLASS_FLAG_SEALED, 1);
          else
             luaX_syntaxerror(ls, "'sealed' export must be followed by 'class'");
        }
        else if (ls->t.token == TK_NAME) {
          localstat(ls, 1);
        }
        else {
          luaX_syntaxerror(ls, "unexpected token after export");
        }
      }
      break;
    }
    case TK_COMMAND: {  /* stat -> commandstat */
      commandstat(ls, line);
      break;
    }
    case TK_KEYWORD: {  /* stat -> keywordstat */
      keywordstat(ls, line);
      break;
    }
    case TK_OPERATOR: {  /* stat -> operatorstat */
      operatorstat(ls, line);
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      luaX_next(ls);  /* skip LOCAL */
      if (testnext(ls, TK_FUNCTION))  /* local function? */
        localfunc(ls, 0);
      else if (testnext(ls, TK_TAKE))  /* local take {...} = expr 解构? */
        takestat_full(ls);
      else
        localstat(ls, 0);
      break;
    }
    case TK_CONST: {  /* stat -> conststat */
      luaX_next(ls);  /* skip CONST */
      if (testnext(ls, TK_FUNCTION))  /* const function? */
        luaK_semerror(ls, "function cannot be declared as const");
      else
        localstat(ls, 0);
      break;
    }
    case TK_GLOBAL: {  /* stat -> globalstatfunc */
      globalstatfunc(ls, line);
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      luaX_next(ls);  /* skip double colon */
      if (ls->t.token == TK_CONTINUE) {
        TString *name = luaS_newliteral(ls->L, "continue");
        luaX_next(ls);
        checknext(ls, TK_DBCOLON);
        labelstat(ls, name, line);
      } else {
        labelstat(ls, str_checkname(ls), line);
      }
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      luaX_next(ls);  /* skip RETURN */
      retstat(ls);
      break;
    }
    case TK_CONTINUE:
    case TK_BREAK: {  /* stat -> breakstat */
      breakstat(ls);
      if(!block_follow(ls,1)){
          luaX_syntaxerror(ls,"break or continue is unreachable statement");
      }
      break;
    }
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      luaX_next(ls);  /* skip 'goto' */
      gotostat(ls);
      break;
    }
    case TK_NAME: {
      /* 使用软关键字系统检查语句开头的软关键字 */
      SoftKWID skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
      if (skw == SKW_CLASS) {
        /* class 作为软关键字，触发类定义解析 */
        classstat(ls, line, 0, 0);  /* 无修饰符 */
        break;
      }
      else if (skw == SKW_INTERFACE) {
        /* interface 作为软关键字，触发接口定义解析 */
        interfacestat(ls, line);
        break;
      }
      else if (skw == SKW_ABSTRACT) {
        /* abstract class 语法 */
        luaX_next(ls);  /* 跳过 'abstract' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_ABSTRACT, 0);
        } else {
          luaX_syntaxerror(ls, "'abstract' 后必须跟 'class'");
        }
        break;
      }
      else if (skw == SKW_FINAL) {
        /* final class 语法 */
        luaX_next(ls);  /* 跳过 'final' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_FINAL, 0);
        } else {
          luaX_syntaxerror(ls, "'final' 后必须跟 'class'");
        }
        break;
      }
      else if (skw == SKW_SEALED) {
        /* sealed class 语法 */
        luaX_next(ls);  /* 跳过 'sealed' */
        SoftKWID next_skw = softkw_check(ls, SOFTKW_CTX_STMT_BEGIN);
        if (next_skw == SKW_CLASS) {
          classstat(ls, line, CLASS_FLAG_SEALED, 0);
        } else {
          luaX_syntaxerror(ls, "'sealed' 后必须跟 'class'");
        }
        break;
      }
  #if defined(LUA_COMPAT_GLOBAL)
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (ls->t.seminfo.ts == ls->glbn) {  /* current = "global"? */
        int lk = luaX_lookahead(ls);
        if (lk == '<' || lk == TK_NAME || lk == '*' || lk == TK_FUNCTION) {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(ls, line);
          break;
        }
      }  /* else... */
  #endif
      /* 不是软关键字，按普通语句处理 */
      exprstat(ls);
      break;
    }
    default: {  /* stat -> func | assignment */
      exprstat(ls);
      break;
    }
  }
  lua_assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
             ls->fs->freereg >= luaY_nvarstack(ls->fs));
  ls->fs->freereg = luaY_nvarstack(ls->fs);  /* free registers */
  leavelevel(ls);
}

/* }=========================================================== */

/* }=========================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static void mainfunc (LexState *ls, FuncState *fs) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(ls, fs, &bl);
  setvararg(fs, 0);  /* main function is always declared vararg */
  env = allocupvalue(fs);  /* ...set environment upvalue */
  env->instack = 1;
  env->idx = 0;
  env->kind = VDKREG;
  env->name = ls->envn;
  luaC_objbarrier(ls->L, fs->f, env->name);
  luaX_next(ls);  /* read first token */
  if(testtoken(ls,'{'))
    retstat(ls);
  else {
    statlist(ls);  /* parse main body */
  }
  check(ls, TK_EOS);
  close_func(ls);
}


LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                       Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  FuncState funcstate;
  LClosure *cl = luaF_newLclosure(L, 1);  /* create main closure */
  setclLvalue2s(L, L->top.p, cl);  /* anchor it (to avoid being collected) */
  luaD_inctop(L);
  lexstate.h = luaH_new(L);  /* create table for scanner */
  sethvalue2s(L, L->top.p, lexstate.h);  /* anchor it */
  luaD_inctop(L);
  funcstate.f = cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);
  funcstate.f->source = luaS_new(L, name);  /* create and anchor TString */
  luaC_objbarrier(L, funcstate.f, funcstate.f->source);
  lexstate.buff = buff;
  lexstate.dyd = dyd;
  lexstate.curpos=0;
  lexstate.tokpos=0;
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  luaX_setinput(L, &lexstate, z, funcstate.f->source, firstchar);
  mainfunc(&lexstate, &funcstate);
  lua_assert(!funcstate.prev && funcstate.nups == 1 && !lexstate.fs);
  /* all scopes should be correctly finished */
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  L->top.p--;  /* remove scanner's table */
  return cl;  /* closure is on the stack, too */
}




