/*
** $Id: lstrlib.c $
** Standard library for string operations and pattern-matching
** See Copyright Notice in lua.h
*/

#define lstrlib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_MALLOC(sz) malloc(sz)
#define STBIW_REALLOC(p,newsz) realloc(p,newsz)
#define STBIW_FREE(p) free(p)
#include "stb_image_write.h"

#include "stb_image.h"


/*
** maximum number of captures that a pattern can do during
** pattern-matching. This limit is arbitrary, but must fit in
** an unsigned char.
*/
#if !defined(LUA_MAXCAPTURES)
#define LUA_MAXCAPTURES		32
#endif


/* macro to 'unsign' a character */
#define uchar(c)	((unsigned char)(c))


/*
** Some sizes are better limited to fit in 'int', but must also fit in
** 'size_t'. (We assume that 'lua_Integer' cannot be smaller than 'int'.)
*/
#define MAX_SIZET	((size_t)(~(size_t)0))

#define MAXSIZE  \
	(sizeof(size_t) < sizeof(int) ? MAX_SIZET : (size_t)(INT_MAX))




static int str_len (lua_State *L) {
  size_t l;
  luaL_checklstring(L, 1, &l);
  lua_pushinteger(L, (lua_Integer)l);
  return 1;
}


/*
** translate a relative initial string position
** (negative means back from end): clip result to [1, inf).
** The length of any string in Lua must fit in a lua_Integer,
** so there are no overflows in the casts.
** The inverted comparison avoids a possible overflow
** computing '-pos'.
*/
static size_t posrelatI (lua_Integer pos, size_t len) {
  if (pos > 0)
    return (size_t)pos;
  else if (pos == 0)
    return 1;
  else if (pos < -(lua_Integer)len)  /* inverted comparison */
    return 1;  /* clip to 1 */
  else return len + (size_t)pos + 1;
}


/*
** Gets an optional ending string position from argument 'arg',
** with default value 'def'.
** Negative means back from end: clip result to [0, len]
*/
static size_t getendpos (lua_State *L, int arg, lua_Integer def,
                         size_t len) {
  lua_Integer pos = luaL_optinteger(L, arg, def);
  if (pos > (lua_Integer)len)
    return len;
  else if (pos >= 0)
    return (size_t)pos;
  else if (pos < -(lua_Integer)len)
    return 0;
  else return len + (size_t)pos + 1;
}

/* translate a relative string position: negative means back from end */
static lua_Integer posrelat (lua_Integer pos, size_t len) {
    if (pos >= 0) return pos;
    else if (0u - (size_t)pos > len) return 0;
    else return (lua_Integer)len + pos + 1;
}

static int str_sub (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  size_t start = posrelatI(luaL_checkinteger(L, 2), l);
  size_t end = getendpos(L, 3, -1, l);
  if (start <= end)
    lua_pushlstring(L, s + start - 1, (end - start) + 1);
  else lua_pushliteral(L, "");
  return 1;
}


static int str_reverse (lua_State *L) {
  size_t l, i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i = 0; i < l; i++)
    p[i] = s[l - i - 1];
  luaL_pushresultsize(&b, l);
  return 1;
}


static int str_lower (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = cast_char(tolower(cast_uchar(s[i])));
  luaL_pushresultsize(&b, l);
  return 1;
}


static int str_upper (lua_State *L) {
  size_t l;
  size_t i;
  luaL_Buffer b;
  const char *s = luaL_checklstring(L, 1, &l);
  char *p = luaL_buffinitsize(L, &b, l);
  for (i=0; i<l; i++)
    p[i] = cast_char(toupper(cast_uchar(s[i])));
  luaL_pushresultsize(&b, l);
  return 1;
}


/*
** MAX_SIZE is limited both by size_t and lua_Integer.
** When x <= MAX_SIZE, x can be safely cast to size_t or lua_Integer.
*/
static int str_rep (lua_State *L) {
  size_t len, lsep;
  const char *s = luaL_checklstring(L, 1, &len);
  lua_Integer n = luaL_checkinteger(L, 2);
  const char *sep = luaL_optlstring(L, 3, "", &lsep);
  if (n <= 0)
    lua_pushliteral(L, "");
  else if (l_unlikely(len > MAX_SIZE - lsep ||
               cast_st2S(len + lsep) > cast_st2S(MAX_SIZE) / n))
    return luaL_error(L, "结果字符串过长");
  else {
    size_t totallen = (cast_sizet(n) * (len + lsep)) - lsep;
    luaL_Buffer b;
    char *p = luaL_buffinitsize(L, &b, totallen);
    while (n-- > 1) {  /* first n-1 copies (followed by separator) */
      memcpy(p, s, len * sizeof(char)); p += len;
      if (lsep > 0) {  /* empty 'memcpy' is not that cheap */
        memcpy(p, sep, lsep * sizeof(char)); p += lsep;
      }
    }
    memcpy(p, s, len * sizeof(char));  /* last copy without separator */
    luaL_pushresultsize(&b, totallen);
  }
  return 1;
}


static int str_byte (lua_State *L) {
  size_t l;
  const char *s = luaL_checklstring(L, 1, &l);
  lua_Integer pi = luaL_optinteger(L, 2, 1);
  size_t posi = posrelatI(pi, l);
  size_t pose = getendpos(L, 3, pi, l);
  int n, i;
  if (posi > pose) return 0;  /* empty interval; return no values */
  if (l_unlikely(pose - posi >= (size_t)INT_MAX))  /* arithmetic overflow? */
    return luaL_error(L, "string slice too long");
  n = (int)(pose -  posi) + 1;
  luaL_checkstack(L, n, "string slice too long");
  for (i=0; i<n; i++)
    lua_pushinteger(L, cast_uchar(s[posi + cast_uint(i) - 1]));
  return n;
}


static int str_char (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  luaL_Buffer b;
  char *p = luaL_buffinitsize(L, &b, cast_uint(n));
  for (i=1; i<=n; i++) {
    lua_Unsigned c = (lua_Unsigned)luaL_checkinteger(L, i);
    luaL_argcheck(L, c <= (lua_Unsigned)UCHAR_MAX, i, "value out of range");
    p[i - 1] = cast_char(cast_uchar(c));
  }
  luaL_pushresultsize(&b, cast_uint(n));
  return 1;
}


/*
** Buffer to store the result of 'string.dump'. It must be initialized
** after the call to 'lua_dump', to ensure that the function is on the
** top of the stack when 'lua_dump' is called. ('luaL_buffinit' might
** push stuff.)
*/
struct str_Writer {
  int init;  /* true iff buffer has been initialized */
  luaL_Buffer B;
};


static int writer (lua_State *L, const void *b, size_t size, void *ud) {
  struct str_Writer *state = (struct str_Writer *)ud;
  if (!state->init) {
    state->init = 1;
    luaL_buffinit(L, &state->B);
  }
  if (b == NULL) {  /* finishing dump? */
    luaL_pushresult(&state->B);  /* push result */
    lua_replace(L, 1);  /* move it to reserved slot */
  }
  else
    luaL_addlstring(&state->B, (const char *)b, size);
  return 0;
}


/*
** string.dump 函数实现
** 将Lua函数导出为字节码字符串
**
** 用法：
**   string.dump(func)                    -- 基本用法
**   string.dump(func, true)              -- 剥离调试信息
**   string.dump(func, {                  -- 表参数形式
**     strip = true,                      -- 是否剥离调试信息
**     obfuscate = 1,                     -- 混淆标志位（可组合）
**     seed = 12345                       -- 随机种子（可选，0或不指定表示使用时间）
**   })
**
** 混淆标志位：
**   0: 不混淆
**   1: 控制流扁平化 (CFF)
**   2: 基本块随机打乱
**   4: 虚假基本块（预留）
**   8: 状态值编码混淆
**
** @param L Lua状态
** @return 1（返回字节码字符串）
*/
static int str_dump (lua_State *L) {
  struct str_Writer state;
  int strip = 0;
  int obfuscate_flags = 0;
  unsigned int seed = 0;
  const char *log_path = NULL;  /* 日志输出路径 */
  
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  /* 检查第二个参数的类型 */
  if (lua_istable(L, 2)) {
    /* 表参数形式：读取各个字段 */
    
    /* 读取 strip 字段 */
    lua_getfield(L, 2, "strip");
    if (!lua_isnil(L, -1)) {
      strip = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 obfuscate 字段（混淆标志位） */
    lua_getfield(L, 2, "obfuscate");
    if (!lua_isnil(L, -1)) {
      obfuscate_flags = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 seed 字段（随机种子） */
    lua_getfield(L, 2, "seed");
    if (!lua_isnil(L, -1)) {
      seed = (unsigned int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    
    /* 读取 log_path 字段（调试日志输出路径）*/
    /* 注意：需要保持字符串在栈上直到 dump 完成 */
    lua_getfield(L, 2, "log_path");
    if (!lua_isnil(L, -1) && lua_isstring(L, -1)) {
      log_path = lua_tostring(L, -1);
      /* 字符串现在在栈顶，保持它在那里 */
    } else {
      lua_pop(L, 1);
      lua_pushnil(L);  /* 占位，保持栈结构一致 */
    }
    /* 现在栈是: [func, table, log_path_or_nil] */
  } else {
    /* 兼容旧的布尔参数形式 */
    strip = lua_toboolean(L, 2);
    lua_pushnil(L);  /* 占位 */
  }
  
  /* 栈: [func, table/bool, log_path_or_nil]
  ** lua_dump 需要函数在栈顶，但我们需要保留 log_path 字符串的引用
  ** 解决方案：把函数复制到栈顶
  */
  lua_pushvalue(L, 1);  /* 复制函数到栈顶 */
  /* 栈: [func, table/bool, log_path_or_nil, func_copy] */
  
  state.init = 0;
  
  int result;
  if (obfuscate_flags != 0) {
    /* 使用带混淆的导出函数 */
    result = lua_dump_obfuscated(L, writer, &state, strip, obfuscate_flags, seed, log_path);
  } else {
    /* 使用普通导出函数 */
    result = lua_dump(L, writer, &state, strip);
  }
  
  if (l_unlikely(result != 0))
    return luaL_error(L, "unable to dump given function1");
  luaL_pushresult(&state.B);
  return 1;
}



/*
** {===========================================
** METAMETHODS
** ============================================
*/

#if defined(LUA_NOCVTS2N)	/* { */

/* no coercion from strings to numbers */

static const luaL_Reg stringmetamethods[] = {
  {"__index", NULL},  /* placeholder */
  {NULL, NULL}
};

#else		/* }{ */

static int tonum (lua_State *L, int arg) {
  if (lua_type(L, arg) == LUA_TNUMBER) {  /* already a number? */
    lua_pushvalue(L, arg);
    return 1;
  }
  else {  /* check whether it is a numerical string */
    size_t len;
    const char *s = lua_tolstring(L, arg, &len);
    return (s != NULL && lua_stringtonumber(L, s) == len + 1);
  }
}


static void trymt (lua_State *L, const char *mtkey, const char *opname) {
  lua_settop(L, 2);  /* back to the original arguments */
  if (l_unlikely(lua_type(L, 2) == LUA_TSTRING ||
                 !luaL_getmetafield(L, 2, mtkey)))
    luaL_error(L, "attempt to %s a '%s' with a '%s'", opname,
                  luaL_typename(L, -2), luaL_typename(L, -1));
  lua_insert(L, -3);  /* put metamethod before arguments */
  lua_call(L, 2, 1);  /* call metamethod */
}

static int arith (lua_State *L, int op, const char *mtname) {
  if (tonum(L, 1) && tonum(L, 2))
    lua_arith(L, op);  /* result will be on the top */
  else
    trymt(L, mtname, mtname + 2);
  return 1;
}
/*
** 字符串拼接元方法
** 允许两个字符串通过 + 操作符进行拼接
*/
static int string_add (lua_State *L) {
  size_t l1, l2;
  const char *s1 = luaL_checklstring(L, 1, &l1);
  const char *s2 = luaL_checklstring(L, 2, &l2);
  
  /* 检查结果字符串长度是否超出限制 */
  if (l_unlikely(l1 > MAX_SIZE - l2))
    return luaL_error(L, "结果字符串过长");
    
  luaL_Buffer b;
  char *p = luaL_buffinitsize(L, &b, l1 + l2);
  memcpy(p, s1, l1);
  memcpy(p + l1, s2, l2);
  luaL_pushresultsize(&b, l1 + l2);
  return 1;
}

/*
** 字符串子串剔除元方法
** 允许通过字符串 - 操作符剔除匹配到的子串（全部匹配）
*/
static int string_sub (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);  /* 原字符串 */
  const char *p = luaL_checklstring(L, 2, &lp);  /* 要剔除的子串 */
  
  if (lp == 0) {  /* 要剔除空字符串，返回原字符串 */
    lua_pushlstring(L, s, ls);
    return 1;
  }
  
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  const char *current = s;
  const char *end = s + ls;
  
  /* 查找并剔除所有匹配的子串 */
  while (current < end) {
    const char *match_pos = (const char *)memchr(current, *p, end - current);
    if (!match_pos) {
      /* 没有找到匹配，添加剩余部分并结束 */
      luaL_addlstring(&b, current, end - current);
      break;
    }
    
    /* 检查是否完全匹配 */
    if (match_pos + lp <= end && memcmp(match_pos, p, lp) == 0) {
      /* 找到完整匹配，添加匹配之前的内容，然后跳过匹配的部分 */
      luaL_addlstring(&b, current, match_pos - current);
      current = match_pos + lp;
    } else {
      /* 不是完整匹配，移动到下一个位置 */
      current = match_pos + 1;
    }
  }
  
  luaL_pushresult(&b);
  return 1;
}

/*
** 字符串索引元方法
** 允许通过数字索引获取对应字节的值
*/
static int string_index (lua_State *L) {
  size_t ls;
  const char *s = luaL_checklstring(L, 1, &ls);  /* 字符串 */
  lua_Integer index = luaL_checkinteger(L, 2);  /* 索引 */
  
  /* 处理负索引（从末尾开始计数） */
  if (index < 0) {
    index = (lua_Integer)ls + index + 1;
  }
  
  /* 检查索引是否有效（Lua 索引从1开始） */
  if (index < 1 || index > (lua_Integer)ls) {
    lua_pushnil(L);  /* 返回nil表示索引无效 */
    return 1;
  }
  
  /* 返回对应位置的字节值 */
  lua_pushinteger(L, (unsigned char)s[index - 1]);
  return 1;
}

static int arith_mul (lua_State *L) {
  return arith(L, LUA_OPMUL, "__mul");
}

static int arith_mod (lua_State *L) {
  return arith(L, LUA_OPMOD, "__mod");
}

static int arith_pow (lua_State *L) {
  return arith(L, LUA_OPPOW, "__pow");
}

static int arith_div (lua_State *L) {
  return arith(L, LUA_OPDIV, "__div");
}

static int arith_idiv (lua_State *L) {
  return arith(L, LUA_OPIDIV, "__idiv");
}

static int arith_unm (lua_State *L) {
  return arith(L, LUA_OPUNM, "__unm");
}


static const luaL_Reg stringmetamethods[] = {
  {"__add", string_add},
  {"__sub", string_sub},
  {"__index", string_index},
  {"__mul", arith_mul},
  {"__mod", arith_mod},
  {"__pow", arith_pow},
  {"__div", arith_div},
  {"__idiv", arith_idiv},
  {"__unm", arith_unm},
  {NULL, NULL}
};

#endif		/* } */

/* }=========================================== */

/*
** {===========================================
** PATTERN MATCHING
** ============================================
*/


#define CAP_UNFINISHED	(-1)
#define CAP_POSITION	(-2)


typedef struct MatchState {
  const char *src_init;  /* init of source string */
  const char *src_end;  /* end ('\0') of source string */
  const char *p_end;  /* end ('\0') of pattern */
  lua_State *L;
  int matchdepth;  /* control for recursive depth (to avoid C stack overflow) */
  int level;  /* total number of captures (finished or unfinished) */
  struct {
    const char *init;
    ptrdiff_t len;  /* length or special value (CAP_*) */
  } capture[LUA_MAXCAPTURES];
} MatchState;


/* recursive function */
static const char *match (MatchState *ms, const char *s, const char *p);


/* maximum recursion depth for 'match' */
#if !defined(MAXCCALLS)
#define MAXCCALLS	200
#endif


#define L_ESC		'%'
#define SPECIALS	"^$*+?.([%-"


static int check_capture (MatchState *ms, int l) {
  l -= '1';
  if (l_unlikely(l < 0 || l >= ms->level ||
                 ms->capture[l].len == CAP_UNFINISHED))
    return luaL_error(ms->L, "无效的捕获索引 %%%d", l + 1);
  return l;
}


static int capture_to_close (MatchState *ms) {
  int level = ms->level;
  for (level--; level>=0; level--)
    if (ms->capture[level].len == CAP_UNFINISHED) return level;
  return luaL_error(ms->L, "无效的模式捕获");
}


static const char *classend (MatchState *ms, const char *p) {
  switch (*p++) {
    case L_ESC: {
      if (l_unlikely(p == ms->p_end))
        luaL_error(ms->L, "格式错误的模式 (以 '%%' 结尾)");
      return p+1;
    }
    case '[': {
      if (*p == '^') p++;
      do {  /* look for a ']' */
        if (l_unlikely(p == ms->p_end))
          luaL_error(ms->L, "格式错误的模式 (缺少 ']')");
        if (*(p++) == L_ESC && p < ms->p_end)
          p++;  /* skip escapes (e.g. '%]') */
      } while (*p != ']');
      return p+1;
    }
    default: {
      return p;
    }
  }
}


static int match_class (int c, int cl) {
  int res;
  switch (tolower(cl)) {
    case 'a' : res = isalpha(c); break;
    case 'c' : res = iscntrl(c); break;
    case 'd' : res = isdigit(c); break;
    case 'g' : res = isgraph(c); break;
    case 'l' : res = islower(c); break;
    case 'p' : res = ispunct(c); break;
    case 's' : res = isspace(c); break;
    case 'u' : res = isupper(c); break;
    case 'w' : res = isalnum(c); break;
    case 'x' : res = isxdigit(c); break;
    case 'z' : res = (c == 0); break;  /* deprecated option */
    default: return (cl == c);
  }
  return (islower(cl) ? res : !res);
}


static int matchbracketclass (int c, const char *p, const char *ec) {
  int sig = 1;
  if (*(p+1) == '^') {
    sig = 0;
    p++;  /* skip the '^' */
  }
  while (++p < ec) {
    if (*p == L_ESC) {
      p++;
      if (match_class(c, cast_uchar(*p)))
        return sig;
    }
    else if ((*(p+1) == '-') && (p+2 < ec)) {
      p+=2;
      if (cast_uchar(*(p-2)) <= c && c <= cast_uchar(*p))
        return sig;
    }
    else if (cast_uchar(*p) == c) return sig;
  }
  return !sig;
}


static int singlematch (MatchState *ms, const char *s, const char *p,
                        const char *ep) {
  if (s >= ms->src_end)
    return 0;
  else {
    int c = cast_uchar(*s);
    switch (*p) {
      case '.': return 1;  /* matches any char */
      case L_ESC: return match_class(c, cast_uchar(*(p+1)));
      case '[': return matchbracketclass(c, p, ep-1);
      default:  return (cast_uchar(*p) == c);
    }
  }
}


static const char *matchbalance (MatchState *ms, const char *s,
                                   const char *p) {
  if (l_unlikely(p >= ms->p_end - 1))
    luaL_error(ms->L, "格式错误的模式 (缺少 '%%b' 的参数)");
  if (*s != *p) return NULL;
  else {
    int b = *p;
    int e = *(p+1);
    int cont = 1;
    while (++s < ms->src_end) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;  /* string ends out of balance */
}


static const char *max_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  ptrdiff_t i = 0;  /* counts maximum expand for item */
  while (singlematch(ms, s + i, p, ep))
    i++;
  /* keeps trying to match with the maximum repetitions */
  while (i>=0) {
    const char *res = match(ms, (s+i), ep+1);
    if (res) return res;
    i--;  /* else didn't match; reduce 1 repetition to try again */
  }
  return NULL;
}


static const char *min_expand (MatchState *ms, const char *s,
                                 const char *p, const char *ep) {
  for (;;) {
    const char *res = match(ms, s, ep+1);
    if (res != NULL)
      return res;
    else if (singlematch(ms, s, p, ep))
      s++;  /* try with one more repetition */
    else return NULL;
  }
}


static const char *start_capture (MatchState *ms, const char *s,
                                    const char *p, int what) {
  const char *res;
  int level = ms->level;
  if (level >= LUA_MAXCAPTURES) luaL_error(ms->L, "捕获过多");
  ms->capture[level].init = s;
  ms->capture[level].len = what;
  ms->level = level+1;
  if ((res=match(ms, s, p)) == NULL)  /* match failed? */
    ms->level--;  /* undo capture */
  return res;
}


static const char *end_capture (MatchState *ms, const char *s,
                                  const char *p) {
  int l = capture_to_close(ms);
  const char *res;
  ms->capture[l].len = s - ms->capture[l].init;  /* close capture */
  if ((res = match(ms, s, p)) == NULL)  /* match failed? */
    ms->capture[l].len = CAP_UNFINISHED;  /* undo capture */
  return res;
}


static const char *match_capture (MatchState *ms, const char *s, int l) {
  size_t len;
  l = check_capture(ms, l);
  len = cast_sizet(ms->capture[l].len);
  if ((size_t)(ms->src_end-s) >= len &&
      memcmp(ms->capture[l].init, s, len) == 0)
    return s+len;
  else return NULL;
}


static const char *match (MatchState *ms, const char *s, const char *p) {
  if (l_unlikely(ms->matchdepth-- == 0))
    luaL_error(ms->L, "模式过于复杂");
  init: /* using goto to optimize tail recursion */
  if (p != ms->p_end) {  /* end of pattern? */
    switch (*p) {
      case '(': {  /* start capture */
        if (*(p + 1) == ')')  /* position capture? */
          s = start_capture(ms, s, p + 2, CAP_POSITION);
        else
          s = start_capture(ms, s, p + 1, CAP_UNFINISHED);
        break;
      }
      case ')': {  /* end capture */
        s = end_capture(ms, s, p + 1);
        break;
      }
      case '$': {
        if ((p + 1) != ms->p_end)  /* is the '$' the last char in pattern? */
          goto dflt;  /* no; go to default */
        s = (s == ms->src_end) ? s : NULL;  /* check end of string */
        break;
      }
      case L_ESC: {  /* escaped sequences not in the format class[*+?-]? */
        switch (*(p + 1)) {
          case 'b': {  /* balanced string? */
            s = matchbalance(ms, s, p + 2);
            if (s != NULL) {
              p += 4; goto init;  /* return match(ms, s, p + 4); */
            }  /* else fail (s == NULL) */
            break;
          }
          case 'f': {  /* frontier? */
            const char *ep; char previous;
            p += 2;
            if (l_unlikely(*p != '['))
              luaL_error(ms->L, "在 '%%f' 后的模式中缺少 '['");
            ep = classend(ms, p);  /* points to what is next */
            previous = (s == ms->src_init) ? '\0' : *(s - 1);
            if (!matchbracketclass(cast_uchar(previous), p, ep - 1) &&
               matchbracketclass(cast_uchar(*s), p, ep - 1)) {
              p = ep; goto init;  /* return match(ms, s, ep); */
            }
            s = NULL;  /* match failed */
            break;
          }
          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6': case '7':
          case '8': case '9': {  /* capture results (%0-%9)? */
            s = match_capture(ms, s, cast_uchar(*(p + 1)));
            if (s != NULL) {
              p += 2; goto init;  /* return match(ms, s, p + 2) */
            }
            break;
          }
          default: goto dflt;
        }
        break;
      }
      default: dflt: {  /* pattern class plus optional suffix */
        const char *ep = classend(ms, p);  /* points to optional suffix */
        /* does not match at least once? */
        if (!singlematch(ms, s, p, ep)) {
          if (*ep == '*' || *ep == '?' || *ep == '-') {  /* accept empty? */
            p = ep + 1; goto init;  /* return match(ms, s, ep + 1); */
          }
          else  /* '+' or no suffix */
            s = NULL;  /* fail */
        }
        else {  /* matched once */
          switch (*ep) {  /* handle optional suffix */
            case '?': {  /* optional */
              const char *res;
              if ((res = match(ms, s + 1, ep + 1)) != NULL)
                s = res;
              else {
                p = ep + 1; goto init;  /* else return match(ms, s, ep + 1); */
              }
              break;
            }
            case '+':  /* 1 or more repetitions */
              s++;  /* 1 match already done */
              /* FALLTHROUGH */
            case '*':  /* 0 or more repetitions */
              s = max_expand(ms, s, p, ep);
              break;
            case '-':  /* 0 or more repetitions (minimum) */
              s = min_expand(ms, s, p, ep);
              break;
            default:  /* no suffix */
              s++; p = ep; goto init;  /* return match(ms, s + 1, ep); */
          }
        }
        break;
      }
    }
  }
  ms->matchdepth++;
  return s;
}



static const char *lmemfind (const char *s1, size_t l1,
                               const char *s2, size_t l2) {
  if (l2 == 0) return s1;  /* empty strings are everywhere */
  else if (l2 > l1) return NULL;  /* avoids a negative 'l1' */
  else {
    const char *init;  /* to search for a '*s2' inside 's1' */
    l2--;  /* 1st char will be checked by 'memchr' */
    l1 = l1-l2;  /* 's2' cannot be found after that */
    while (l1 > 0 && (init = (const char *)memchr(s1, *s2, l1)) != NULL) {
      init++;   /* 1st char is already checked */
      if (memcmp(init, s2+1, l2) == 0)
        return init-1;
      else {  /* correct 'l1' and 's1' to try again */
        l1 -= ct_diff2sz(init - s1);
        s1 = init;
      }
    }
    return NULL;  /* not found */
  }
}


/*
** get information about the i-th capture. If there are no captures
** and 'i==0', return information about the whole match, which
** is the range 's'..'e'. If the capture is a string, return
** its length and put its address in '*cap'. If it is an integer
** (a position), push it on the stack and return CAP_POSITION.
*/
static ptrdiff_t get_onecapture (MatchState *ms, int i, const char *s,
                              const char *e, const char **cap) {
  if (i >= ms->level) {
    if (l_unlikely(i != 0))
      luaL_error(ms->L, "无效的捕获索引 %%%d", i + 1);
    *cap = s;
    return (e - s);
  }
  else {
    ptrdiff_t capl = ms->capture[i].len;
    *cap = ms->capture[i].init;
    if (l_unlikely(capl == CAP_UNFINISHED))
      luaL_error(ms->L, "未完成的捕获");
    else if (capl == CAP_POSITION)
      lua_pushinteger(ms->L,
          ct_diff2S(ms->capture[i].init - ms->src_init) + 1);
    return capl;
  }
}


/*
** Push the i-th capture on the stack.
*/
static void push_onecapture (MatchState *ms, int i, const char *s,
                                                    const char *e) {
  const char *cap;
  ptrdiff_t l = get_onecapture(ms, i, s, e, &cap);
  if (l != CAP_POSITION)
    lua_pushlstring(ms->L, cap, cast_sizet(l));
  /* else position was already pushed */
}


static int push_captures (MatchState *ms, const char *s, const char *e) {
  int i;
  int nlevels = (ms->level == 0 && s) ? 1 : ms->level;
  luaL_checkstack(ms->L, nlevels, "too many captures");
  for (i = 0; i < nlevels; i++)
    push_onecapture(ms, i, s, e);
  return nlevels;  /* number of strings pushed */
}


/* check whether pattern has no special characters */
static int nospecials (const char *p, size_t l) {
  size_t upto = 0;
  do {
    if (strpbrk(p + upto, SPECIALS))
      return 0;  /* pattern has a special character */
    upto += strlen(p + upto) + 1;  /* may have more after \0 */
  } while (upto <= l);
  return 1;  /* no special chars found */
}


static void prepstate (MatchState *ms, lua_State *L,
                       const char *s, size_t ls, const char *p, size_t lp) {
  ms->L = L;
  ms->matchdepth = MAXCCALLS;
  ms->src_init = s;
  ms->src_end = s + ls;
  ms->p_end = p + lp;
}


static void reprepstate (MatchState *ms) {
  ms->level = 0;
  lua_assert(ms->matchdepth == MAXCCALLS);
}


static int str_find_aux (lua_State *L, int find) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  if (init > ls) {  /* start after string's end? */
    luaL_pushfail(L);  /* cannot find anything */
    return 1;
  }
  /* explicit request or no special characters? */
  if (find && (lua_toboolean(L, 4) || nospecials(p, lp))) {
    /* do a plain search */
    const char *s2 = lmemfind(s + init, ls - init, p, lp);
    if (s2) {
      lua_pushinteger(L, ct_diff2S(s2 - s) + 1);
      lua_pushinteger(L, cast_st2S(ct_diff2sz(s2 - s) + lp));
      return 2;
    }
  }
  else {
    MatchState ms;
    const char *s1 = s + init;
    int anchor = (*p == '^');
    if (anchor) {
      p++; lp--;  /* skip anchor character */
    }
    prepstate(&ms, L, s, ls, p, lp);
    do {
      const char *res;
      reprepstate(&ms);
      if ((res=match(&ms, s1, p)) != NULL) {
        if (find) {
          lua_pushinteger(L, ct_diff2S(s1 - s) + 1);  /* start */
          lua_pushinteger(L, ct_diff2S(res - s));   /* end */
          return push_captures(&ms, NULL, 0) + 2;
        }
        else
          return push_captures(&ms, s1, res);
      }
    } while (s1++ < ms.src_end && !anchor);
  }
  luaL_pushfail(L);  /* not found */
  return 1;
}


static int str_find (lua_State *L) {
  return str_find_aux(L, 1);
}


//mod DifierLine
static int gfind_aux (lua_State *L) {
    size_t ls, lp;
    const char *s = lua_tolstring(L, lua_upvalueindex(1), &ls);
    const char *p = lua_tolstring(L, lua_upvalueindex(2), &lp);
    lua_Integer init = posrelat(luaL_optinteger(L, lua_upvalueindex(3), 1), ls);
    if (init < 1) init = 1;
    else if (init > (lua_Integer)ls + 1) {  /* start after string's end? */
        return 0;  /* cannot find anything */
    }
    /* explicit request or no special characters? */
    if (lua_toboolean(L, lua_upvalueindex(4)) || nospecials(p, lp)) {
        /* do a plain search */
        const char *s2 = lmemfind(s + init - 1, ls - (size_t)init + 1, p, lp);
        if (s2) {
            lua_pushinteger(L, (s2 - s) + 1);
            lua_pushinteger(L, (s2 - s) + lp);
            lua_pushinteger(L,(s2 - s) + lp + 1);
            lua_replace(L, lua_upvalueindex(3));
            return 2;
        }
    }
    else {
        MatchState ms;
        const char *s1 = s + init - 1;
        int anchor = (*p == '^');
        if (anchor) {
            p++; lp--;  /* skip anchor character */
        }
        ms.L = L;
        ms.matchdepth = MAXCCALLS;
        ms.src_init = s;
        ms.src_end = s + ls;
        ms.p_end = p + lp;
        do {
            const char *res;
            ms.level = 0;
            lua_assert(ms.matchdepth == MAXCCALLS);
            if ((res=match(&ms, s1, p)) != NULL) {
                lua_pushinteger(L, (s1 - s) + 1);  /* start */
                lua_pushinteger(L, res - s);   /* end */
                lua_pushinteger(L, res - s + 1);
                lua_replace(L, lua_upvalueindex(3));
                return push_captures(&ms, NULL, 0) + 2;
            }
        } while (s1++ < ms.src_end && !anchor);
    }
    return 0;  /* not found */
}


static int gfind (lua_State *L) {
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    int b = lua_toboolean(L, 3);
    lua_settop(L, 2);
    lua_pushinteger(L, 0);
    lua_pushboolean(L, b);
    lua_pushcclosure(L, gfind_aux, 4);
    return 1;
}


static int str_match (lua_State *L) {
  return str_find_aux(L, 0);
}


/* state for 'gmatch' */
typedef struct GMatchState {
  const char *src;  /* current position */
  const char *p;  /* pattern */
  const char *lastmatch;  /* end of last match */
  MatchState ms;  /* match state */
} GMatchState;


static int gmatch_aux (lua_State *L) {
  GMatchState *gm = (GMatchState *)lua_touserdata(L, lua_upvalueindex(3));
  const char *src;
  gm->ms.L = L;
  for (src = gm->src; src <= gm->ms.src_end; src++) {
    const char *e;
    reprepstate(&gm->ms);
    if ((e = match(&gm->ms, src, gm->p)) != NULL && e != gm->lastmatch) {
      gm->src = gm->lastmatch = e;
      return push_captures(&gm->ms, src, e);
    }
  }
  return 0;  /* not found */
}


static int gmatch (lua_State *L) {
  size_t ls, lp;
  const char *s = luaL_checklstring(L, 1, &ls);
  const char *p = luaL_checklstring(L, 2, &lp);
  size_t init = posrelatI(luaL_optinteger(L, 3, 1), ls) - 1;
  GMatchState *gm;
  lua_settop(L, 2);  /* keep strings on closure to avoid being collected */
  gm = (GMatchState *)lua_newuserdatauv(L, sizeof(GMatchState), 0);
  if (init > ls)  /* start after string's end? */
    init = ls + 1;  /* avoid overflows in 's + init' */
  prepstate(&gm->ms, L, s, ls, p, lp);
  gm->src = s + init; gm->p = p; gm->lastmatch = NULL;
  lua_pushcclosure(L, gmatch_aux, 3);
  return 1;
}


static void add_s (MatchState *ms, luaL_Buffer *b, const char *s,
                                                   const char *e) {
  size_t l;
  lua_State *L = ms->L;
  const char *news = lua_tolstring(L, 3, &l);
  const char *p;
  while ((p = (char *)memchr(news, L_ESC, l)) != NULL) {
    luaL_addlstring(b, news, ct_diff2sz(p - news));
    p++;  /* skip ESC */
    if (*p == L_ESC)  /* '%%' */
      luaL_addchar(b, *p);
    else if (*p == '0')  /* '%0' */
        luaL_addlstring(b, s, ct_diff2sz(e - s));
    else if (isdigit(cast_uchar(*p))) {  /* '%n' */
      const char *cap;
      ptrdiff_t resl = get_onecapture(ms, *p - '1', s, e, &cap);
      if (resl == CAP_POSITION)
        luaL_addvalue(b);  /* add position to accumulated result */
      else
        luaL_addlstring(b, cap, cast_sizet(resl));
    }
    else
      luaL_error(L, "替换字符串中无效使用 '%c'", L_ESC);
    l -= ct_diff2sz(p + 1 - news);
    news = p + 1;
  }
  luaL_addlstring(b, news, l);
}


/*
** Add the replacement value to the string buffer 'b'.
** Return true if the original string was changed. (Function calls and
** table indexing resulting in nil or false do not change the subject.)
*/
static int add_value (MatchState *ms, luaL_Buffer *b, const char *s,
                                      const char *e, int tr) {
  lua_State *L = ms->L;
  switch (tr) {
    case LUA_TFUNCTION: {  /* call the function */
      int n;
      lua_pushvalue(L, 3);  /* push the function */
      n = push_captures(ms, s, e);  /* all captures as arguments */
      lua_call(L, n, 1);  /* call it */
      break;
    }
    case LUA_TTABLE: {  /* index the table */
      push_onecapture(ms, 0, s, e);  /* first capture is the index */
      lua_gettable(L, 3);
      break;
    }
    default: {  /* LUA_TNUMBER or LUA_TSTRING */
      add_s(ms, b, s, e);  /* add value to the buffer */
      return 1;  /* something changed */
    }
  }
  if (!lua_toboolean(L, -1)) {  /* nil or false? */
    lua_pop(L, 1);  /* remove value */
    luaL_addlstring(b, s, ct_diff2sz(e - s));  /* keep original text */
    return 0;  /* no changes */
  }
  else if (l_unlikely(!lua_isstring(L, -1)))
    return luaL_error(L, "invalid replacement value (a %s)",
                         luaL_typename(L, -1));
  else {
    luaL_addvalue(b);  /* add result to accumulator */
    return 1;  /* something changed */
  }
}


static int str_gsub (lua_State *L) {
  size_t srcl, lp;
  const char *src = luaL_checklstring(L, 1, &srcl);  /* subject */
  const char *p = luaL_checklstring(L, 2, &lp);  /* pattern */
  const char *lastmatch = NULL;  /* end of last match */
  int tr = lua_type(L, 3);  /* replacement type */
  /* max replacements */
  lua_Integer max_s = luaL_optinteger(L, 4, cast_st2S(srcl) + 1);
  int anchor = (*p == '^');
  lua_Integer n = 0;  /* replacement count */
  int changed = 0;  /* change flag */
  MatchState ms;
  luaL_Buffer b;
  luaL_argexpected(L, tr == LUA_TNUMBER || tr == LUA_TSTRING ||
                   tr == LUA_TFUNCTION || tr == LUA_TTABLE, 3,
                      "string/function/table");
  luaL_buffinit(L, &b);
  if (anchor) {
    p++; lp--;  /* skip anchor character */
  }
  prepstate(&ms, L, src, srcl, p, lp);
  while (n < max_s) {
    const char *e;
    reprepstate(&ms);  /* (re)prepare state for new match */
    if ((e = match(&ms, src, p)) != NULL && e != lastmatch) {  /* match? */
      n++;
      changed = add_value(&ms, &b, src, e, tr) | changed;
      src = lastmatch = e;
    }
    else if (src < ms.src_end)  /* otherwise, skip one character */
      luaL_addchar(&b, *src++);
    else break;  /* end of subject */
    if (anchor) break;
  }
  if (!changed)  /* no changes? */
    lua_pushvalue(L, 1);  /* return original string */
  else {  /* something changed */
    luaL_addlstring(&b, src, ct_diff2sz(ms.src_end - src));
    luaL_pushresult(&b);  /* create and return new string */
  }
  lua_pushinteger(L, n);  /* number of substitutions */
  return 2;
}

/* }=========================================== */



/*
** {===========================================
** STRING FORMAT
** ============================================
*/

#if !defined(lua_number2strx)	/* { */

/*
** Hexadecimal floating-point formatter
*/

#define SIZELENMOD	(sizeof(LUA_NUMBER_FRMLEN)/sizeof(char))


/*
** Number of bits that goes into the first digit. It can be any value
** between 1 and 4; the following definition tries to align the number
** to nibble boundaries by making what is left after that first digit a
** multiple of 4.
*/
#define L_NBFD		((l_floatatt(MANT_DIG) - 1)%4 + 1)


/*
** Add integer part of 'x' to buffer and return new 'x'
*/
static lua_Number adddigit (char *buff, unsigned n, lua_Number x) {
  lua_Number dd = l_mathop(floor)(x);  /* get integer part from 'x' */
  int d = (int)dd;
  buff[n] = cast_char(d < 10 ? d + '0' : d - 10 + 'a');  /* add to buffer */
  return x - dd;  /* return what is left */
}


static int num2straux (char *buff, unsigned sz, lua_Number x) {
  /* if 'inf' or 'NaN', format it like '%g' */
  if (x != x || x == (lua_Number)HUGE_VAL || x == -(lua_Number)HUGE_VAL)
    return l_sprintf(buff, sz, LUA_NUMBER_FMT, (LUAI_UACNUMBER)x);
  else if (x == 0) {  /* can be -0... */
    /* create "0" or "-0" followed by exponent */
    return l_sprintf(buff, sz, LUA_NUMBER_FMT "x0p+0", (LUAI_UACNUMBER)x);
  }
  else {
    int e;
    lua_Number m = l_mathop(frexp)(x, &e);  /* 'x' fraction and exponent */
    unsigned n = 0;  /* character count */
    if (m < 0) {  /* is number negative? */
      buff[n++] = '-';  /* add sign */
      m = -m;  /* make it positive */
    }
    buff[n++] = '0'; buff[n++] = 'x';  /* add "0x" */
    m = adddigit(buff, n++, m * (1 << L_NBFD));  /* add first digit */
    e -= L_NBFD;  /* this digit goes before the radix point */
    if (m > 0) {  /* more digits? */
      buff[n++] = lua_getlocaledecpoint();  /* add radix point */
      do {  /* add as many digits as needed */
        m = adddigit(buff, n++, m * 16);
      } while (m > 0);
    }
    n += cast_uint(l_sprintf(buff + n, sz - n, "p%+d", e));  /* add exponent */
    lua_assert(n < sz);
    return cast_int(n);
  }
}


static int lua_number2strx (lua_State *L, char *buff, unsigned sz,
                            const char *fmt, lua_Number x) {
  int n = num2straux(buff, sz, x);
  if (fmt[SIZELENMOD] == 'A') {
    int i;
    for (i = 0; i < n; i++)
      buff[i] = cast_char(toupper(cast_uchar(buff[i])));
  }
  else if (l_unlikely(fmt[SIZELENMOD] != 'a'))
    return luaL_error(L, "格式 '%%a'/'%%A' 的修饰符未实现");
  return n;
}

#endif				/* } */


/*
** Maximum size for items formatted with '%f'. This size is produced
** by format('%.99f', -maxfloat), and is equal to 99 + 3 ('-', '.',
** and '\0') + number of decimal digits to represent maxfloat (which
** is maximum exponent + 1). (99+3+1, adding some extra, 110)
*/
#define MAX_ITEMF	(110 + l_floatatt(MAX_10_EXP))


/*
** All formats except '%f' do not need that large limit.  The other
** float formats use exponents, so that they fit in the 99 limit for
** significant digits; 's' for large strings and 'q' add items directly
** to the buffer; all integer formats also fit in the 99 limit.  The
** worst case are floats: they may need 99 significant digits, plus
** '0x', '-', '.', 'e+XXXX', and '\0'. Adding some extra, 120.
*/
#define MAX_ITEM	120


/* valid flags in a format specification */
#if !defined(L_FMTFLAGSF)

/* valid flags for a, A, e, E, f, F, g, and G conversions */
#define L_FMTFLAGSF	"-+#0 "

/* valid flags for o, x, and X conversions */
#define L_FMTFLAGSX	"-#0"

/* valid flags for d and i conversions */
#define L_FMTFLAGSI	"-+0 "

/* valid flags for u conversions */
#define L_FMTFLAGSU	"-0"

/* valid flags for c, p, and s conversions */
#define L_FMTFLAGSC	"-"

#endif


/*
** Maximum size of each format specification (such as "%-099.99d"):
** Initial '%', flags (up to 5), width (2), period, precision (2),
** length modifier (8), conversion specifier, and final '\0', plus some
** extra.
*/
#define MAX_FORMAT	32


static void addquoted (luaL_Buffer *b, const char *s, size_t len) {
  luaL_addchar(b, '"');
  while (len--) {
    if (*s == '"' || *s == '\\' || *s == '\n') {
      luaL_addchar(b, '\\');
      luaL_addchar(b, *s);
    }
    else if (iscntrl(cast_uchar(*s))) {
      char buff[10];
      if (!isdigit(cast_uchar(*(s+1))))
        l_sprintf(buff, sizeof(buff), "\\%d", (int)cast_uchar(*s));
      else
        l_sprintf(buff, sizeof(buff), "\\%03d", (int)cast_uchar(*s));
      luaL_addstring(b, buff);
    }
    else
      luaL_addchar(b, *s);
    s++;
  }
  luaL_addchar(b, '"');
}


/*
** Serialize a floating-point number in such a way that it can be
** scanned back by Lua. Use hexadecimal format for "common" numbers
** (to preserve precision); inf, -inf, and NaN are handled separately.
** (NaN cannot be expressed as a numeral, so we write '(0/0)' for it.)
*/
static int quotefloat (lua_State *L, char *buff, lua_Number n) {
  const char *s;  /* for the fixed representations */
  if (n == (lua_Number)HUGE_VAL)  /* inf? */
    s = "1e9999";
  else if (n == -(lua_Number)HUGE_VAL)  /* -inf? */
    s = "-1e9999";
  else if (n != n)  /* NaN? */
    s = "(0/0)";
  else {  /* format number as hexadecimal */
    int  nb = lua_number2strx(L, buff, MAX_ITEM,
                                 "%" LUA_NUMBER_FRMLEN "a", n);
    /* ensures that 'buff' string uses a dot as the radix character */
    if (memchr(buff, '.', cast_uint(nb)) == NULL) {  /* no dot? */
      char point = lua_getlocaledecpoint();  /* try locale point */
      char *ppoint = (char *)memchr(buff, point, cast_uint(nb));
      if (ppoint) *ppoint = '.';  /* change it to a dot */
    }
    return nb;
  }
  /* for the fixed representations */
  return l_sprintf(buff, MAX_ITEM, "%s", s);
}


static void addliteral (lua_State *L, luaL_Buffer *b, int arg) {
  switch (lua_type(L, arg)) {
    case LUA_TSTRING: {
      size_t len;
      const char *s = lua_tolstring(L, arg, &len);
      addquoted(b, s, len);
      break;
    }
    case LUA_TNUMBER: {
      char *buff = luaL_prepbuffsize(b, MAX_ITEM);
      int nb;
      if (!lua_isinteger(L, arg))  /* float? */
        nb = quotefloat(L, buff, lua_tonumber(L, arg));
      else {  /* integers */
        lua_Integer n = lua_tointeger(L, arg);
        const char *format = (n == LUA_MININTEGER)  /* corner case? */
                           ? "0x%" LUA_INTEGER_FRMLEN "x"  /* use hex */
                           : LUA_INTEGER_FMT;  /* else use default format */
        nb = l_sprintf(buff, MAX_ITEM, format, (LUAI_UACINT)n);
      }
      luaL_addsize(b, cast_uint(nb));
      break;
    }
    case LUA_TNIL: case LUA_TBOOLEAN: {
      luaL_tolstring(L, arg, NULL);
      luaL_addvalue(b);
      break;
    }
    default: {
      luaL_argerror(L, arg, "value has no literal form");
    }
  }
}


static const char *get2digits (const char *s) {
  if (isdigit(cast_uchar(*s))) {
    s++;
    if (isdigit(cast_uchar(*s))) s++;  /* (2 digits at most) */
  }
  return s;
}


/*
** Check whether a conversion specification is valid. When called,
** first character in 'form' must be '%' and last character must
** be a valid conversion specifier. 'flags' are the accepted flags;
** 'precision' signals whether to accept a precision.
*/
static void checkformat (lua_State *L, const char *form, const char *flags,
                                       int precision) {
  const char *spec = form + 1;  /* skip '%' */
  spec += strspn(spec, flags);  /* skip flags */
  if (*spec != '0') {  /* a width cannot start with '0' */
    spec = get2digits(spec);  /* skip width */
    if (*spec == '.' && precision) {
      spec++;
      spec = get2digits(spec);  /* skip precision */
    }
  }
  if (!isalpha(cast_uchar(*spec)))  /* did not go to the end? */
    luaL_error(L, "无效的转换规范: '%s'", form);
}


/*
** Get a conversion specification and copy it to 'form'.
** Return the address of its last character.
*/
static const char *getformat (lua_State *L, const char *strfrmt,
                                            char *form) {
  /* spans flags, width, and precision ('0' is included as a flag) */
  size_t len = strspn(strfrmt, L_FMTFLAGSF "123456789.");
  len++;  /* adds following character (should be the specifier) */
  /* still needs space for '%', '\0', plus a length modifier */
  if (len >= MAX_FORMAT - 10)
    luaL_error(L, "无效的格式 (过长)");
  *(form++) = '%';
  memcpy(form, strfrmt, len * sizeof(char));
  *(form + len) = '\0';
  return strfrmt + len - 1;
}


/*
** add length modifier into formats
*/
static void addlenmod (char *form, const char *lenmod) {
  size_t l = strlen(form);
  size_t lm = strlen(lenmod);
  char spec = form[l - 1];
  strcpy(form + l - 1, lenmod);
  form[l + lm - 1] = spec;
  form[l + lm] = '\0';
}


static int str_format (lua_State *L) {
  int top = lua_gettop(L);
  int arg = 1;
  size_t sfl;
  const char *strfrmt = luaL_checklstring(L, arg, &sfl);
  const char *strfrmt_end = strfrmt+sfl;
  const char *flags;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while (strfrmt < strfrmt_end) {
    if (*strfrmt != L_ESC)
      luaL_addchar(&b, *strfrmt++);
    else if (*++strfrmt == L_ESC)
      luaL_addchar(&b, *strfrmt++);  /* %% */
    else { /* format item */
      char form[MAX_FORMAT];  /* to store the format ('%...') */
      unsigned maxitem = MAX_ITEM;  /* maximum length for the result */
      char *buff = luaL_prepbuffsize(&b, maxitem);  /* to put result */
      int nb = 0;  /* number of bytes in result */
      if (++arg > top)
        return luaL_argerror(L, arg, "no value");
      strfrmt = getformat(L, strfrmt, form);
      switch (*strfrmt++) {
        case 'c': {
          checkformat(L, form, L_FMTFLAGSC, 0);
          nb = l_sprintf(buff, maxitem, form, (int)luaL_checkinteger(L, arg));
          break;
        }
        case 'd': case 'i':
          flags = L_FMTFLAGSI;
          goto intcase;
        case 'u':
          flags = L_FMTFLAGSU;
          goto intcase;
        case 'o': case 'x': case 'X':
          flags = L_FMTFLAGSX;
         intcase: {
          lua_Integer n = luaL_checkinteger(L, arg);
          checkformat(L, form, flags, 1);
          addlenmod(form, LUA_INTEGER_FRMLEN);
          nb = l_sprintf(buff, maxitem, form, (LUAI_UACINT)n);
          break;
        }
        case 'a': case 'A':
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, LUA_NUMBER_FRMLEN);
          nb = lua_number2strx(L, buff, maxitem, form,
                                  luaL_checknumber(L, arg));
          break;
        case 'f':
          maxitem = MAX_ITEMF;  /* extra space for '%f' */
          buff = luaL_prepbuffsize(&b, maxitem);
          /* FALLTHROUGH */
        case 'e': case 'E': case 'g': case 'G': {
          lua_Number n = luaL_checknumber(L, arg);
          checkformat(L, form, L_FMTFLAGSF, 1);
          addlenmod(form, LUA_NUMBER_FRMLEN);
          nb = l_sprintf(buff, maxitem, form, (LUAI_UACNUMBER)n);
          break;
        }
        case 'p': {
          const void *p = lua_topointer(L, arg);
          checkformat(L, form, L_FMTFLAGSC, 0);
          if (p == NULL) {  /* avoid calling 'printf' with argument NULL */
            p = "(null)";  /* result */
            form[strlen(form) - 1] = 's';  /* format it as a string */
          }
          nb = l_sprintf(buff, maxitem, form, p);
          break;
        }
        case 'q': {
          if (form[2] != '\0')  /* modifiers? */
            return luaL_error(L, "说明符 '%%q' 不能有修饰符");
          addliteral(L, &b, arg);
          break;
        }
        case 's': {
          size_t l;
          const char *s = luaL_tolstring(L, arg, &l);
          if (form[2] == '\0')  /* no modifiers? */
            luaL_addvalue(&b);  /* keep entire string */
          else {
            luaL_argcheck(L, l == strlen(s), arg, "string contains zeros");
            checkformat(L, form, L_FMTFLAGSC, 1);
            if (strchr(form, '.') == NULL && l >= 100) {
              /* no precision and string is too long to be formatted */
              luaL_addvalue(&b);  /* keep entire string */
            }
            else {  /* format the string into 'buff' */
              nb = l_sprintf(buff, maxitem, form, s);
              lua_pop(L, 1);  /* remove result from 'luaL_tolstring' */
            }
          }
          break;
        }
        default: {  /* also treat cases 'pnLlh' */
          return luaL_error(L, "无效的转换 '%s' 到 'format'", form);
        }
      }
      lua_assert(cast_uint(nb) < maxitem);
      luaL_addsize(&b, cast_uint(nb));
    }
  }
  luaL_pushresult(&b);
  return 1;
}

/* }=========================================== */


/*
** {===========================================
** PACK/UNPACK
** ============================================
*/


/* value used for padding */
#if !defined(LUAL_PACKPADBYTE)
#define LUAL_PACKPADBYTE		0x00
#endif

/* maximum size for the binary representation of an integer */
#define MAXINTSIZE	16

/* number of bits in a character */
#define NB	CHAR_BIT

/* mask for one character (NB 1's) */
#define MC	((1 << NB) - 1)

/* size of a lua_Integer */
#define SZINT	((int)sizeof(lua_Integer))


/* dummy union to get native endianness */
static const union {
  int dummy;
  char little;  /* true iff machine is little endian */
} nativeendian = {1};


/*
** information to pack/unpack stuff
*/
typedef struct Header {
  lua_State *L;
  int islittle;
  unsigned maxalign;
} Header;


/*
** options for pack/unpack
*/
typedef enum KOption {
  Kint,		/* signed integers */
  Kuint,	/* unsigned integers */
  Kfloat,	/* single-precision floating-point numbers */
  Knumber,	/* Lua "native" floating-point numbers */
  Kdouble,	/* double-precision floating-point numbers */
  Kchar,	/* fixed-length strings */
  Kstring,	/* strings with prefixed length */
  Kzstr,	/* zero-terminated strings */
  Kpadding,	/* padding */
  Kpaddalign,	/* padding for alignment */
  Knop		/* no-op (configuration or spaces) */
} KOption;


/*
** Read an integer numeral from string 'fmt' or return 'df' if
** there is no numeral
*/
static int digit (int c) { return '0' <= c && c <= '9'; }

static size_t getnum (const char **fmt, size_t df) {
  if (!digit(**fmt))  /* no number? */
    return df;  /* return default value */
  else {
    size_t a = 0;
    do {
      a = a*10 + cast_uint(*((*fmt)++) - '0');
    } while (digit(**fmt) && a <= (MAX_SIZE - 9)/10);
    return a;
  }
}


/*
** Read an integer numeral and raises an error if it is larger
** than the maximum size of integers.
*/
static unsigned getnumlimit (Header *h, const char **fmt, size_t df) {
  size_t sz = getnum(fmt, df);
  if (l_unlikely((sz - 1u) >= MAXINTSIZE))
    return cast_uint(luaL_error(h->L,
               "integral size (%d) out of limits [1,%d]", sz, MAXINTSIZE));
  return cast_uint(sz);
}


/*
** Initialize Header
*/
static void initheader (lua_State *L, Header *h) {
  h->L = L;
  h->islittle = nativeendian.little;
  h->maxalign = 1;
}


/*
** Read and classify next option. 'size' is filled with option's size.
*/
static KOption getoption (Header *h, const char **fmt, size_t *size) {
  /* dummy structure to get native alignment requirements */
  struct cD { char c; union { LUAI_MAXALIGN; } u; };
  int opt = *((*fmt)++);
  *size = 0;  /* default */
  switch (opt) {
    case 'b': *size = sizeof(char); return Kint;
    case 'B': *size = sizeof(char); return Kuint;
    case 'h': *size = sizeof(short); return Kint;
    case 'H': *size = sizeof(short); return Kuint;
    case 'l': *size = sizeof(long); return Kint;
    case 'L': *size = sizeof(long); return Kuint;
    case 'j': *size = sizeof(lua_Integer); return Kint;
    case 'J': *size = sizeof(lua_Integer); return Kuint;
    case 'T': *size = sizeof(size_t); return Kuint;
    case 'f': *size = sizeof(float); return Kfloat;
    case 'n': *size = sizeof(lua_Number); return Knumber;
    case 'd': *size = sizeof(double); return Kdouble;
    case 'i': *size = getnumlimit(h, fmt, sizeof(int)); return Kint;
    case 'I': *size = getnumlimit(h, fmt, sizeof(int)); return Kuint;
    case 's': *size = getnumlimit(h, fmt, sizeof(size_t)); return Kstring;
    case 'c':
      *size = getnum(fmt, cast_sizet(-1));
      if (l_unlikely(*size == cast_sizet(-1)))
        luaL_error(h->L, "格式选项 'c' 缺少大小");
      return Kchar;
    case 'z': return Kzstr;
    case 'x': *size = 1; return Kpadding;
    case 'X': return Kpaddalign;
    case ' ': break;
    case '<': h->islittle = 1; break;
    case '>': h->islittle = 0; break;
    case '=': h->islittle = nativeendian.little; break;
    case '!': {
      const size_t maxalign = offsetof(struct cD, u);
      h->maxalign = getnumlimit(h, fmt, maxalign);
      break;
    }
    default: luaL_error(h->L, "无效的格式选项 '%c'", opt);
  }
  return Knop;
}


/*
** Read, classify, and fill other details about the next option.
** 'psize' is filled with option's size, 'notoalign' with its
** alignment requirements.
** Local variable 'size' gets the size to be aligned. (Kpadal option
** always gets its full alignment, other options are limited by
** the maximum alignment ('maxalign'). Kchar option needs no alignment
** despite its size.
*/
static KOption getdetails (Header *h, size_t totalsize, const char **fmt,
                           size_t *psize, unsigned *ntoalign) {
  KOption opt = getoption(h, fmt, psize);
  size_t align = *psize;  /* usually, alignment follows size */
  if (opt == Kpaddalign) {  /* 'X' gets alignment from following option */
    if (**fmt == '\0' || getoption(h, fmt, &align) == Kchar || align == 0)
      luaL_argerror(h->L, 1, "invalid next option for option 'X'");
  }
  if (align <= 1 || opt == Kchar)  /* need no alignment? */
    *ntoalign = 0;
  else {
    if (align > h->maxalign)  /* enforce maximum alignment */
      align = h->maxalign;
    if (l_unlikely(!ispow2(align))) {  /* not a power of 2? */
      *ntoalign = 0;  /* to avoid warnings */
      luaL_argerror(h->L, 1, "format asks for alignment not power of 2");
    }
    else {
      /* 'szmoda' = totalsize % align */
      unsigned szmoda = cast_uint(totalsize & (align - 1));
      *ntoalign = cast_uint((align - szmoda) & (align - 1));
    }
  }
  return opt;
}


/*
** Pack integer 'n' with 'size' bytes and 'islittle' endianness.
** The final 'if' handles the case when 'size' is larger than
** the size of a Lua integer, correcting the extra sign-extension
** bytes if necessary (by default they would be zeros).
*/
static void packint (luaL_Buffer *b, lua_Unsigned n,
                     int islittle, unsigned size, int neg) {
  char *buff = luaL_prepbuffsize(b, size);
  unsigned i;
  buff[islittle ? 0 : size - 1] = (char)(n & MC);  /* first byte */
  for (i = 1; i < size; i++) {
    n >>= NB;
    buff[islittle ? i : size - 1 - i] = (char)(n & MC);
  }
  if (neg && size > SZINT) {  /* negative number need sign extension? */
    for (i = SZINT; i < size; i++)  /* correct extra bytes */
      buff[islittle ? i : size - 1 - i] = (char)MC;
  }
  luaL_addsize(b, size);  /* add result to buffer */
}


/*
** Copy 'size' bytes from 'src' to 'dest', correcting endianness if
** given 'islittle' is different from native endianness.
*/
static void copywithendian (char *dest, const char *src,
                            unsigned size, int islittle) {
  if (islittle == nativeendian.little)
    memcpy(dest, src, size);
  else {
    dest += size - 1;
    while (size-- != 0)
      *(dest--) = *(src++);
  }
}


static int str_pack (lua_State *L) {
  luaL_Buffer b;
  Header h;
  const char *fmt = luaL_checkstring(L, 1);  /* format string */
  int arg = 1;  /* current argument to pack */
  size_t totalsize = 0;  /* accumulate total size of result */
  initheader(L, &h);
  lua_pushnil(L);  /* mark to separate arguments from string buffer */
  luaL_buffinit(L, &b);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    luaL_argcheck(L, size + ntoalign <= MAX_SIZE - totalsize, arg,
                     "result too long");
    totalsize += ntoalign + size;
    while (ntoalign-- > 0)
     luaL_addchar(&b, LUAL_PACKPADBYTE);  /* fill alignment */
    arg++;
    switch (opt) {
      case Kint: {  /* signed integers */
        lua_Integer n = luaL_checkinteger(L, arg);
        if (size < SZINT) {  /* need overflow check? */
          lua_Integer lim = (lua_Integer)1 << ((size * NB) - 1);
          luaL_argcheck(L, -lim <= n && n < lim, arg, "integer overflow");
        }
        packint(&b, (lua_Unsigned)n, h.islittle, cast_uint(size), (n < 0));
        break;
      }
      case Kuint: {  /* unsigned integers */
        lua_Integer n = luaL_checkinteger(L, arg);
        if (size < SZINT)  /* need overflow check? */
          luaL_argcheck(L, (lua_Unsigned)n < ((lua_Unsigned)1 << (size * NB)),
                           arg, "unsigned overflow");
        packint(&b, (lua_Unsigned)n, h.islittle, cast_uint(size), 0);
        break;
      }
      case Kfloat: {  /* C float */
        float f = (float)luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Knumber: {  /* Lua float */
        lua_Number f = luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Kdouble: {  /* C double */
        double f = (double)luaL_checknumber(L, arg);  /* get argument */
        char *buff = luaL_prepbuffsize(&b, sizeof(f));
        /* move 'f' to final result, correcting endianness if needed */
        copywithendian(buff, (char *)&f, sizeof(f), h.islittle);
        luaL_addsize(&b, size);
        break;
      }
      case Kchar: {  /* fixed-size string */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, len <= size, arg, "string longer than given size");
        luaL_addlstring(&b, s, len);  /* add string */
        if (len < size) {  /* does it need padding? */
          size_t psize = size - len;  /* pad size */
          char *buff = luaL_prepbuffsize(&b, psize);
          memset(buff, LUAL_PACKPADBYTE, psize);
          luaL_addsize(&b, psize);
        }
        break;
      }
      case Kstring: {  /* strings with length count */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, size >= sizeof(lua_Unsigned) ||
                         len < ((lua_Unsigned)1 << (size * NB)),
                         arg, "string length does not fit in given size");
        /* pack length */
        packint(&b, (lua_Unsigned)len, h.islittle, cast_uint(size), 0);
        luaL_addlstring(&b, s, len);
        totalsize += len;
        break;
      }
      case Kzstr: {  /* zero-terminated string */
        size_t len;
        const char *s = luaL_checklstring(L, arg, &len);
        luaL_argcheck(L, strlen(s) == len, arg, "string contains zeros");
        luaL_addlstring(&b, s, len);
        luaL_addchar(&b, '\0');  /* add zero at the end */
        totalsize += len + 1;
        break;
      }
      case Kpadding: luaL_addchar(&b, LUAL_PACKPADBYTE);  /* FALLTHROUGH */
      case Kpaddalign: case Knop:
        arg--;  /* undo increment */
        break;
    }
  }
  luaL_pushresult(&b);
  return 1;
}


static int str_packsize (lua_State *L) {
  Header h;
  const char *fmt = luaL_checkstring(L, 1);  /* format string */
  size_t totalsize = 0;  /* accumulate total size of result */
  initheader(L, &h);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, totalsize, &fmt, &size, &ntoalign);
    luaL_argcheck(L, opt != Kstring && opt != Kzstr, 1,
                     "variable-length format");
    size += ntoalign;  /* total space used by option */
    luaL_argcheck(L, totalsize <= LUA_MAXINTEGER - size,
                     1, "format result too large");
    totalsize += size;
  }
  lua_pushinteger(L, cast_st2S(totalsize));
  return 1;
}


/*
** Unpack an integer with 'size' bytes and 'islittle' endianness.
** If size is smaller than the size of a Lua integer and integer
** is signed, must do sign extension (propagating the sign to the
** higher bits); if size is larger than the size of a Lua integer,
** it must check the unread bytes to see whether they do not cause an
** overflow.
*/
static lua_Integer unpackint (lua_State *L, const char *str,
                              int islittle, int size, int issigned) {
  lua_Unsigned res = 0;
  int i;
  int limit = (size  <= SZINT) ? size : SZINT;
  for (i = limit - 1; i >= 0; i--) {
    res <<= NB;
    res |= (lua_Unsigned)(unsigned char)str[islittle ? i : size - 1 - i];
  }
  if (size < SZINT) {  /* real size smaller than lua_Integer? */
    if (issigned) {  /* needs sign extension? */
      lua_Unsigned mask = (lua_Unsigned)1 << (size*NB - 1);
      res = ((res ^ mask) - mask);  /* do sign extension */
    }
  }
  else if (size > SZINT) {  /* must check unread bytes */
    int mask = (!issigned || (lua_Integer)res >= 0) ? 0 : MC;
    for (i = limit; i < size; i++) {
      if (l_unlikely((unsigned char)str[islittle ? i : size - 1 - i] != mask))
        luaL_error(L, "%d 字节整数不适合 Lua 整数", size);
    }
  }
  return (lua_Integer)res;
}


static int str_unpack (lua_State *L) {
  Header h;
  const char *fmt = luaL_checkstring(L, 1);
  size_t ld;
  const char *data = luaL_checklstring(L, 2, &ld);
  size_t pos = posrelatI(luaL_optinteger(L, 3, 1), ld) - 1;
  int n = 0;  /* number of results */
  luaL_argcheck(L, pos <= ld, 3, "initial position out of string");
  initheader(L, &h);
  while (*fmt != '\0') {
    unsigned ntoalign;
    size_t size;
    KOption opt = getdetails(&h, pos, &fmt, &size, &ntoalign);
    luaL_argcheck(L, ntoalign + size <= ld - pos, 2,
                    "data string too short");
    pos += ntoalign;  /* skip alignment */
    /* stack space for item + next position */
    luaL_checkstack(L, 2, "too many results");
    n++;
    switch (opt) {
      case Kint:
      case Kuint: {
        lua_Integer res = unpackint(L, data + pos, h.islittle,
                                       cast_int(size), (opt == Kint));
        lua_pushinteger(L, res);
        break;
      }
      case Kfloat: {
        float f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, (lua_Number)f);
        break;
      }
      case Knumber: {
        lua_Number f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, f);
        break;
      }
      case Kdouble: {
        double f;
        copywithendian((char *)&f, data + pos, sizeof(f), h.islittle);
        lua_pushnumber(L, (lua_Number)f);
        break;
      }
      case Kchar: {
        lua_pushlstring(L, data + pos, size);
        break;
      }
      case Kstring: {
        lua_Unsigned len = (lua_Unsigned)unpackint(L, data + pos,
                                          h.islittle, cast_int(size), 0);
        luaL_argcheck(L, len <= ld - pos - size, 2, "data string too short");
        lua_pushlstring(L, data + pos + size, cast_sizet(len));
        pos += cast_sizet(len);  /* skip string */
        break;
      }
      case Kzstr: {
        size_t len = strlen(data + pos);
        luaL_argcheck(L, pos + len < ld, 2,
                         "unfinished string for format 'z'");
        lua_pushlstring(L, data + pos, len);
        pos += len + 1;  /* skip string plus final '\0' */
        break;
      }
      case Kpaddalign: case Kpadding: case Knop:
        n--;  /* undo increment */
        break;
    }
    pos += size;
  }
  lua_pushinteger(L, cast_st2S(pos) + 1);  /* next position */
  return n + 1;
}

/* }=========================================== */

#include <math.h>

typedef struct {
  unsigned char *data;
  long len;
  long max_len;
} PngWriteContext;

static void png_write_callback(void *context, void *data, int size) {
  PngWriteContext *ctx = (PngWriteContext *)context;
  long needed_len = ctx->len + size;
  if (needed_len > ctx->max_len) {
    // 动态扩展内存，确保有足够的空间
    long new_max_len = needed_len * 2;
    unsigned char *new_data = (unsigned char *)realloc(ctx->data, new_max_len);
    if (!new_data) {
      // 内存分配失败，直接返回，后续会处理错误
      return;
    }
    ctx->data = new_data;
    ctx->max_len = new_max_len;
  }
  memcpy(ctx->data + ctx->len, data, size);
  ctx->len += size;
}

extern int stbi_write_png_compression_level;
extern int stbi_write_force_png_filter;

/**
 * 将文件内容转换为PNG图像数据（彩色）
 * @param L Lua状态机
 * @return 返回PNG图像数据（字符串）或错误信息
 * 参数:
 *   arg1: 文件路径 (string)
 *   arg2: 输出图像路径 (string, optional) - 如果提供则保存到文件，否则返回图像数据
 *   arg3: 图像宽度 (integer, optional, default: 256)
 */
static int str_file2png (lua_State *L) {
  stbi_write_png_compression_level = 0;
  stbi_write_force_png_filter = 0;
  const char *file_path = luaL_checkstring(L, 1);
  const char *output_path = luaL_optstring(L, 2, NULL);
  lua_Integer width = luaL_optinteger(L, 3, 256);
  
  if (width <= 0) {
    return luaL_error(L, "图像宽度必须大于0");
  }
  
  FILE *fp = fopen(file_path, "rb");
  if (!fp) {
    return luaL_error(L, "无法打开文件: %s", file_path);
  }
  
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  if (file_size < 0) {
    fclose(fp);
    return luaL_error(L, "无法获取文件大小");
  }
  
  unsigned char *file_data = (unsigned char *)malloc(file_size);
  if (!file_data) {
    fclose(fp);
    return luaL_error(L, "内存分配失败");
  }
  
  size_t read_size = fread(file_data, 1, file_size, fp);
  fclose(fp);
  
  if (read_size != (size_t)file_size) {
    free(file_data);
    return luaL_error(L, "读取文件失败");
  }
  
  int img_width = (int)width;
  // 正确计算图像高度：每个字节对应一个像素，每个像素3字节RGB
  long img_height_long = (file_size + (long)img_width - 1) / (long)img_width;
  
  // 确保高度不超过int的最大值，避免溢出
  int img_height = (int)img_height_long;
  if (img_height_long > 2147483647L) {
    img_height = 2147483647;
  }
  
  // 使用 long 类型计算内存大小，避免溢出
  long image_data_size = (long)img_width * img_height * 3;
  unsigned char *image_data = (unsigned char *)malloc(image_data_size);
  if (!image_data) {
    free(file_data);
    return luaL_error(L, "内存分配失败");
  }
  
  memset(image_data, 0, image_data_size);
  
  // 处理所有字节，确保完整转换
  for (long i = 0; i < file_size; i++) {
    int x = i % img_width;
    int y = i / img_width;
    int img_idx = (y * img_width + x) * 3;
    unsigned char byte = file_data[i];
    
    // 按照用户要求的三通道存储模式
    int channel_index;
    int pos = i % 9;
    if (pos == 0) channel_index = 0;    // 第1字节 → 通道1
    else if (pos == 1) channel_index = 1;   // 第2字节 → 通道2
    else if (pos == 2) channel_index = 2;   // 第3字节 → 通道3
    else if (pos == 3) channel_index = 2;   // 第4字节 → 通道3
    else if (pos == 4) channel_index = 1;   // 第5字节 → 通道2
    else if (pos == 5) channel_index = 0;   // 第6字节 → 通道1
    else if (pos == 6) channel_index = 2;   // 第7字节 → 通道3
    else if (pos == 7) channel_index = 1;   // 第8字节 → 通道2
    else channel_index = 0;   // 第9字节 → 通道1
    image_data[img_idx + channel_index] = byte ^ 0x55;
    // 其他通道填充0，确保图像数据正确
    if (channel_index != 0) image_data[img_idx] = 0;
    if (channel_index != 1) image_data[img_idx + 1] = 0;
    if (channel_index != 2) image_data[img_idx + 2] = 0;
  }
  
  free(file_data);
  
  if (output_path) {
    int result = stbi_write_png(output_path, img_width, img_height, 3, image_data, img_width * 3);
    free(image_data);
    
    if (!result) {
      return luaL_error(L, "写入PNG文件失败: %s", output_path);
    }
    
    lua_pushboolean(L, 1);
    return 1;
  } else {
    PngWriteContext ctx = {0};
    // 使用 long 类型计算初始内存大小，避免溢出
    ctx.max_len = (long)img_width * img_height * 4;
    ctx.data = (unsigned char *)malloc(ctx.max_len);
    
    int result = stbi_write_png_to_func(png_write_callback, &ctx, img_width, img_height, 3, image_data, img_width * 3);
    free(image_data);
    
    if (!result || ctx.len <= 0) {
      free(ctx.data);
      return luaL_error(L, "生成PNG数据失败");
    }
    
    lua_pushlstring(L, (const char *)ctx.data, ctx.len);
    free(ctx.data);
    return 1;
  }
}

/**
 * 将PNG图像数据或文件转换回原始文件
 * @param L Lua状态机
 * @return 返回1表示成功，错误返回nil+错误信息
 * 参数:
 *   arg1: PNG图像路径或PNG数据 (string)
 *   arg2: 输出文件路径 (string)
 *   arg3: 原始文件大小 (integer, optional) - 如果提供则只读取指定字节数
 */
static int str_png2file (lua_State *L) {
  const char *png_input = luaL_checkstring(L, 1);
  const char *output_path = luaL_checkstring(L, 2);
  lua_Integer original_size = luaL_optinteger(L, 3, 0);
  
  int img_width, img_height, img_comp;
  unsigned char *image_data = NULL;
  
  const char *png_data = NULL;
  size_t png_len = 0;
  
  if (lua_gettop(L) >= 1 && lua_type(L, 1) == LUA_TSTRING) {
    const char *input = lua_tolstring(L, 1, &png_len);
    if (png_len > 8) {
      unsigned char png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
      if (memcmp(input, png_sig, 8) == 0) {
        image_data = stbi_load_from_memory((const unsigned char *)input, (int)png_len, &img_width, &img_height, &img_comp, STBI_rgb);
      }
    }
  }
  
  if (!image_data) {
    image_data = stbi_load(png_input, &img_width, &img_height, &img_comp, STBI_rgb);
  }
  
  if (!image_data) {
    return luaL_error(L, "无法加载PNG图像: %s", png_input);
  }
  
  long max_bytes = (long)img_width * img_height;
  long actual_size;
  
  if (original_size > 0) {
    if (original_size > max_bytes) {
      stbi_image_free(image_data);
      return luaL_error(L, "原始文件大小(%ld)超过图像容量(%ld)", (long)original_size, max_bytes);
    }
    actual_size = (long)original_size;
  } else {
    actual_size = max_bytes;
  }
  
  FILE *fp = fopen(output_path, "wb");
  if (!fp) {
    stbi_image_free(image_data);
    return luaL_error(L, "无法创建输出文件: %s", output_path);
  }
  
  for (long i = 0; i < actual_size; i++) {
    int x = i % img_width;
    int y = i / img_width;
    int img_idx = (y * img_width + x) * 3;
    // 按照用户要求的三通道存储模式
    int channel_index;
    int pos = i % 9;
    if (pos == 0) channel_index = 0;    // 第1字节 → 通道1
    else if (pos == 1) channel_index = 1;   // 第2字节 → 通道2
    else if (pos == 2) channel_index = 2;   // 第3字节 → 通道3
    else if (pos == 3) channel_index = 2;   // 第4字节 → 通道3
    else if (pos == 4) channel_index = 1;   // 第5字节 → 通道2
    else if (pos == 5) channel_index = 0;   // 第6字节 → 通道1
    else if (pos == 6) channel_index = 2;   // 第7字节 → 通道3
    else if (pos == 7) channel_index = 1;   // 第8字节 → 通道2
    else channel_index = 0;   // 第9字节 → 通道1
    unsigned char byte = image_data[img_idx + channel_index] ^ 0x55;
    if (fwrite(&byte, 1, 1, fp) != 1) {
      fclose(fp);
      stbi_image_free(image_data);
      return luaL_error(L, "写入文件失败");
    }
  }
  
  fclose(fp);
  stbi_image_free(image_data);
  
  lua_pushboolean(L, 1);
  return 1;
}

/**
 * 读取文件内容
 * @param L Lua状态机
 * @return 返回文件内容（字符串）或错误信息
 * 参数:
 *   arg1: 文件路径 (string)
 */
static int str_file (lua_State *L) {
  const char *file_path = luaL_checkstring(L, 1);
  
  FILE *fp = fopen(file_path, "rb");
  if (!fp) {
    return luaL_error(L, "无法打开文件: %s", file_path);
  }
  
  fseek(fp, 0, SEEK_END);
  long file_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  if (file_size < 0) {
    fclose(fp);
    return luaL_error(L, "无法获取文件大小");
  }
  
  unsigned char *file_data = (unsigned char *)malloc(file_size);
  if (!file_data) {
    fclose(fp);
    return luaL_error(L, "内存分配失败");
  }
  
  size_t read_size = fread(file_data, 1, file_size, fp);
  fclose(fp);
  
  if (read_size != (size_t)file_size) {
    free(file_data);
    return luaL_error(L, "读取文件失败");
  }
  
  lua_pushlstring(L, (const char *)file_data, file_size);
  free(file_data);
  return 1;
}

/**
 * 将数据转换为PNG图像数据
 * @param L Lua状态机
 * @return 返回PNG图像数据（字符串）
 * 参数:
 *   arg1: 原始数据 (string)
 *   arg2: 图像宽度 (integer, optional, default: 256)
 */
static int str_data2png (lua_State *L) {
  stbi_write_png_compression_level = 0;
  size_t data_size;
  const char *data = luaL_checklstring(L, 1, &data_size);
  lua_Integer width = luaL_optinteger(L, 2, 256);
  
  if (width <= 0) {
    return luaL_error(L, "图像宽度必须大于0");
  }
  
  int img_width = (int)width;
  // 正确计算图像高度：每个字节对应一个像素，每个像素3字节RGB
  long img_height_long = (data_size + (long)img_width - 1) / (long)img_width;
  // 确保高度不超过int范围
  int img_height = (int)img_height_long;
  if (img_height != img_height_long) {
    return luaL_error(L, "图像高度超出范围");
  }
  
  unsigned char *image_data = (unsigned char *)malloc((size_t)img_width * img_height * 3);
  if (!image_data) {
    return luaL_error(L, "内存分配失败");
  }
  
  memset(image_data, 0, img_width * img_height * 3);
  
  for (size_t i = 0; i < data_size; i++) {
    int x = i % img_width;
    int y = i / img_width;
    int img_idx = (y * img_width + x) * 3;
    unsigned char byte = ((const unsigned char *)data)[i];
    // 按照用户要求的三通道存储模式
    int channel_index;
    int pos = i % 9;
    if (pos == 0) channel_index = 0;    // 第1字节 → 通道1
    else if (pos == 1) channel_index = 1;   // 第2字节 → 通道2
    else if (pos == 2) channel_index = 2;   // 第3字节 → 通道3
    else if (pos == 3) channel_index = 2;   // 第4字节 → 通道3
    else if (pos == 4) channel_index = 1;   // 第5字节 → 通道2
    else if (pos == 5) channel_index = 0;   // 第6字节 → 通道1
    else if (pos == 6) channel_index = 2;   // 第7字节 → 通道3
    else if (pos == 7) channel_index = 1;   // 第8字节 → 通道2
    else channel_index = 0;   // 第9字节 → 通道1
    image_data[img_idx + channel_index] = byte ^ 0x55;
    // 其他通道填充0，确保图像数据正确
    if (channel_index != 0) image_data[img_idx] = 0;
    if (channel_index != 1) image_data[img_idx + 1] = 0;
    if (channel_index != 2) image_data[img_idx + 2] = 0;
  }
  
  PngWriteContext ctx = {0};
  ctx.max_len = img_width * img_height * 4;
  ctx.data = (unsigned char *)malloc(ctx.max_len);
  if (!ctx.data) {
    free(image_data);
    return luaL_error(L, "内存分配失败");
  }
  
  int result = stbi_write_png_to_func(png_write_callback, &ctx, img_width, img_height, 3, image_data, img_width * 3);
  free(image_data);
  
  if (!result || ctx.len <= 0) {
    free(ctx.data);
    return luaL_error(L, "生成PNG数据失败");
  }
  
  lua_pushlstring(L, (const char *)ctx.data, ctx.len);
  free(ctx.data);
  return 1;
}

/**
 * 将PNG图像数据转换回原始数据
 * @param L Lua状态机
 * @return 返回原始数据（字符串）
 * 参数:
 *   arg1: PNG数据 (string)
 *   arg2: 原始数据大小 (integer, optional) - 如果提供则只返回指定字节数
 */
static int str_png2data (lua_State *L) {
  size_t png_len;
  const char *png_data = luaL_checklstring(L, 1, &png_len);
  lua_Integer original_size = luaL_optinteger(L, 2, 0);
  
  int img_width, img_height, img_comp;
  unsigned char *image_data = stbi_load_from_memory((const unsigned char *)png_data, (int)png_len, &img_width, &img_height, &img_comp, STBI_rgb);
  
  if (!image_data) {
    return luaL_error(L, "无法加载PNG图像数据");
  }
  
  long expected_size = (long)img_width * img_height;
  long actual_size = original_size > 0 ? original_size : expected_size;
  
  if (actual_size > expected_size) {
    stbi_image_free(image_data);
    return luaL_error(L, "原始数据大小超过PNG容量");
  }
  
  unsigned char *result_data = (unsigned char *)malloc(actual_size);
  if (!result_data) {
    stbi_image_free(image_data);
    return luaL_error(L, "内存分配失败");
  }
  
  for (long i = 0; i < actual_size; i++) {
    int x = i % img_width;
    int y = i / img_width;
    int img_idx = (y * img_width + x) * 3;
    // 按照用户要求的三通道存储模式
    int channel_index;
    int pos = i % 9;
    if (pos == 0) channel_index = 0;    // 第1字节 → 通道1
    else if (pos == 1) channel_index = 1;   // 第2字节 → 通道2
    else if (pos == 2) channel_index = 2;   // 第3字节 → 通道3
    else if (pos == 3) channel_index = 2;   // 第4字节 → 通道3
    else if (pos == 4) channel_index = 1;   // 第5字节 → 通道2
    else if (pos == 5) channel_index = 0;   // 第6字节 → 通道1
    else if (pos == 6) channel_index = 2;   // 第7字节 → 通道3
    else if (pos == 7) channel_index = 1;   // 第8字节 → 通道2
    else channel_index = 0;   // 第9字节 → 通道1
    result_data[i] = image_data[img_idx + channel_index] ^ 0x55;
  }
  
  stbi_image_free(image_data);
  lua_pushlstring(L, (const char *)result_data, actual_size);
  free(result_data);
  return 1;
}

/**
 * 将PNG图像数据还原并作为Lua代码执行
 * @param L Lua状态机
 * @return 返回执行结果
 * 参数:
 *   arg1: PNG数据 (string)
 */
static int str_data (lua_State *L) {
  size_t png_len;
  const char *png_data = luaL_checklstring(L, 1, &png_len);
  
  int img_width, img_height, img_comp;
  unsigned char *image_data = stbi_load_from_memory((const unsigned char *)png_data, (int)png_len, &img_width, &img_height, &img_comp, STBI_rgb);
  
  if (!image_data) {
    return luaL_error(L, "无法加载PNG图像数据");
  }
  
  long expected_size = (long)img_width * img_height;
  
  unsigned char *result_data = (unsigned char *)malloc(expected_size);
  if (!result_data) {
    stbi_image_free(image_data);
    return luaL_error(L, "内存分配失败");
  }
  
  for (long i = 0; i < expected_size; i++) {
    int x = i % img_width;
    int y = i / img_width;
    int img_idx = (y * img_width + x) * 3;
    // 按照用户要求的三通道存储模式
    int channel_index;
    int pos = i % 9;
    if (pos == 0) channel_index = 0;    // 第1字节 → 通道1
    else if (pos == 1) channel_index = 1;   // 第2字节 → 通道2
    else if (pos == 2) channel_index = 2;   // 第3字节 → 通道3
    else if (pos == 3) channel_index = 2;   // 第4字节 → 通道3
    else if (pos == 4) channel_index = 1;   // 第5字节 → 通道2
    else if (pos == 5) channel_index = 0;   // 第6字节 → 通道1
    else if (pos == 6) channel_index = 2;   // 第7字节 → 通道3
    else if (pos == 7) channel_index = 1;   // 第8字节 → 通道2
    else channel_index = 0;   // 第9字节 → 通道1
    result_data[i] = image_data[img_idx + channel_index] ^ 0x55;
  }
  
  stbi_image_free(image_data);
  
  int status = luaL_loadbuffer(L, (const char *)result_data, expected_size, "=(PNG data)");
  free(result_data);
  
  if (status != LUA_OK) {
    return lua_error(L);
  }
  
  return lua_pcall(L, 0, LUA_MULTRET, 0);
}

static const luaL_Reg strlib[] = {
  {"byte", str_byte},
  {"char", str_char},
  {"data", str_data},
  {"data2png", str_data2png},
  {"dump", str_dump},
  {"file", str_file},
  {"file2png", str_file2png},
  {"find", str_find},
  {"gfind", gfind},
  {"format", str_format},
  {"gmatch", gmatch},
  {"gsub", str_gsub},
  {"len", str_len},
  {"lower", str_lower},
  {"match", str_match},
  {"png2data", str_png2data},
  {"png2file", str_png2file},
  {"rep", str_rep},
  {"reverse", str_reverse},
  {"sub", str_sub},
  {"upper", str_upper},
  {"pack", str_pack},
  {"packsize", str_packsize},
  {"unpack", str_unpack},
  {NULL, NULL}
};


static void createmetatable (lua_State *L) {
  /* table to be metatable for strings */
  luaL_newlibtable(L, stringmetamethods);
  luaL_setfuncs(L, stringmetamethods, 0);
  lua_pushliteral(L, "");  /* dummy string */
  lua_pushvalue(L, -2);  /* copy table */
  lua_setmetatable(L, -2);  /* set table as metatable for strings */
  lua_pop(L, 1);  /* pop dummy string */
  lua_pushvalue(L, -2);  /* get string library */
  lua_setfield(L, -2, "__index");  /* metatable.__index = string */
  lua_pop(L, 1);  /* pop metatable */
}


/*
** Open string library
*/
LUAMOD_API int luaopen_string (lua_State *L) {
  luaL_newlib(L, strlib);
  createmetatable(L);
  return 1;
}

