/*
** $Id: lbaselib.c $
** Basic library
** See Copyright Notice in lua.h
*/

#define lbaselib_c
#define LUA_LIB

#include "lprefix.h"


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "llimits.h"
#include "ltable.h"
#include "lfunc.h"
#include "ldo.h"
#include "lgc.h"
#include "lclass.h"
#include "lapi.h"
#include <stdint.h>

#if defined(__ANDROID__) && !defined(__NDK_MAJOR__)
#include <android/log.h>
#define LOG_TAG "lua"
#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

/* 声明libc库的初始化函数 */
extern int luaopen_libc(lua_State *L);

/* 声明logtable模块的初始化函数 */
extern int luaopen_logtable(lua_State *L);

/*
** MD5 算法实现（基于 Lua 版本移植）
** 产生128位（16字节）哈希值，输出为32位十六进制字符串
** 使用独特的命名避免与 Lua 宏冲突
*/

static uint32_t md5_func_f(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | ((~x) & z); }
static uint32_t md5_func_g(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & (~z)); }
static uint32_t md5_func_h(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static uint32_t md5_func_i(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | (~z)); }

static uint32_t md5_rotate_left(uint32_t x, int s) {
  return (((x & 0xffffffff) << s) | ((x & 0xffffffff) >> (32 - s))) & 0xffffffff;
}

static uint32_t md5_round_ff(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
  a = a + md5_func_f(b, c, d) + x + ac;
  a = md5_rotate_left(a, s) + b;
  return a & 0xffffffff;
}

static uint32_t md5_round_gg(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
  a = a + md5_func_g(b, c, d) + x + ac;
  a = md5_rotate_left(a, s) + b;
  return a & 0xffffffff;
}

static uint32_t md5_round_hh(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
  a = a + md5_func_h(b, c, d) + x + ac;
  a = md5_rotate_left(a, s) + b;
  return a & 0xffffffff;
}

static uint32_t md5_round_ii(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, int s, uint32_t ac) {
  a = a + md5_func_i(b, c, d) + x + ac;
  a = md5_rotate_left(a, s) + b;
  return a & 0xffffffff;
}

static void md5_process_block(uint32_t result[4], const uint32_t block[16]) {
  uint32_t a = result[0];
  uint32_t b = result[1];
  uint32_t c = result[2];
  uint32_t d = result[3];

  a = md5_round_ff(a, b, c, d, block[0], 7, 0xd76aa478);
  d = md5_round_ff(d, a, b, c, block[1], 12, 0xe8c7b756);
  c = md5_round_ff(c, d, a, b, block[2], 17, 0x242070db);
  b = md5_round_ff(b, c, d, a, block[3], 22, 0xc1bdceee);
  a = md5_round_ff(a, b, c, d, block[4], 7, 0xf57c0faf);
  d = md5_round_ff(d, a, b, c, block[5], 12, 0x4787c62a);
  c = md5_round_ff(c, d, a, b, block[6], 17, 0xa8304613);
  b = md5_round_ff(b, c, d, a, block[7], 22, 0xfd469501);
  a = md5_round_ff(a, b, c, d, block[8], 7, 0x698098d8);
  d = md5_round_ff(d, a, b, c, block[9], 12, 0x8b44f7af);
  c = md5_round_ff(c, d, a, b, block[10], 17, 0xffff5bb1);
  b = md5_round_ff(b, c, d, a, block[11], 22, 0x895cd7be);
  a = md5_round_ff(a, b, c, d, block[12], 7, 0x6b901122);
  d = md5_round_ff(d, a, b, c, block[13], 12, 0xfd987193);
  c = md5_round_ff(c, d, a, b, block[14], 17, 0xa679438e);
  b = md5_round_ff(b, c, d, a, block[15], 22, 0x49b40821);

  a = md5_round_gg(a, b, c, d, block[1], 5, 0xf61e2562);
  d = md5_round_gg(d, a, b, c, block[6], 9, 0xc040b340);
  c = md5_round_gg(c, d, a, b, block[11], 14, 0x265e5a51);
  b = md5_round_gg(b, c, d, a, block[0], 20, 0xe9b6c7aa);
  a = md5_round_gg(a, b, c, d, block[5], 5, 0xd62f105d);
  d = md5_round_gg(d, a, b, c, block[10], 9, 0x02441453);
  c = md5_round_gg(c, d, a, b, block[15], 14, 0xd8a1e681);
  b = md5_round_gg(b, c, d, a, block[4], 20, 0xe7d3fbc8);
  a = md5_round_gg(a, b, c, d, block[9], 5, 0x21e1cde6);
  d = md5_round_gg(d, a, b, c, block[14], 9, 0xc33707d6);
  c = md5_round_gg(c, d, a, b, block[3], 14, 0xf4d50d87);
  b = md5_round_gg(b, c, d, a, block[8], 20, 0x455a14ed);
  a = md5_round_gg(a, b, c, d, block[13], 5, 0xa9e3e905);
  d = md5_round_gg(d, a, b, c, block[2], 9, 0xfcefa3f8);
  c = md5_round_gg(c, d, a, b, block[7], 14, 0x676f02d9);
  b = md5_round_gg(b, c, d, a, block[12], 20, 0x8d2a4c8a);

  a = md5_round_hh(a, b, c, d, block[5], 4, 0xfffa3942);
  d = md5_round_hh(d, a, b, c, block[8], 11, 0x8771f681);
  c = md5_round_hh(c, d, a, b, block[11], 16, 0x6d9d6122);
  b = md5_round_hh(b, c, d, a, block[14], 23, 0xfde5380c);
  a = md5_round_hh(a, b, c, d, block[1], 4, 0xa4beea44);
  d = md5_round_hh(d, a, b, c, block[4], 11, 0x4bdecfa9);
  c = md5_round_hh(c, d, a, b, block[7], 16, 0xf6bb4b60);
  b = md5_round_hh(b, c, d, a, block[10], 23, 0xbebfbc70);
  a = md5_round_hh(a, b, c, d, block[13], 4, 0x289b7ec6);
  d = md5_round_hh(d, a, b, c, block[0], 11, 0xeaa127fa);
  c = md5_round_hh(c, d, a, b, block[3], 16, 0xd4ef3085);
  b = md5_round_hh(b, c, d, a, block[6], 23, 0x04881d05);
  a = md5_round_hh(a, b, c, d, block[9], 4, 0xd9d4d039);
  d = md5_round_hh(d, a, b, c, block[12], 11, 0xe6db99e5);
  c = md5_round_hh(c, d, a, b, block[15], 16, 0x1fa27cf8);
  b = md5_round_hh(b, c, d, a, block[2], 23, 0xc4ac5665);

  a = md5_round_ii(a, b, c, d, block[0], 6, 0xf4292244);
  d = md5_round_ii(d, a, b, c, block[7], 10, 0x432aff97);
  c = md5_round_ii(c, d, a, b, block[14], 15, 0xab9423a7);
  b = md5_round_ii(b, c, d, a, block[5], 21, 0xfc93a039);
  a = md5_round_ii(a, b, c, d, block[12], 6, 0x655b59c3);
  d = md5_round_ii(d, a, b, c, block[3], 10, 0x8f0ccc92);
  c = md5_round_ii(c, d, a, b, block[10], 15, 0xffeff47d);
  b = md5_round_ii(b, c, d, a, block[1], 21, 0x85845dd1);
  a = md5_round_ii(a, b, c, d, block[8], 6, 0x6fa87e4f);
  d = md5_round_ii(d, a, b, c, block[15], 10, 0xfe2ce6e0);
  c = md5_round_ii(c, d, a, b, block[6], 15, 0xa3014314);
  b = md5_round_ii(b, c, d, a, block[13], 21, 0x4e0811a1);
  a = md5_round_ii(a, b, c, d, block[4], 6, 0xf7537e82);
  d = md5_round_ii(d, a, b, c, block[11], 10, 0xbd3af235);
  c = md5_round_ii(c, d, a, b, block[2], 15, 0x2ad7d2bb);
  b = md5_round_ii(b, c, d, a, block[9], 21, 0xeb86d391);

  result[0] = (result[0] + a) & 0xffffffff;
  result[1] = (result[1] + b) & 0xffffffff;
  result[2] = (result[2] + c) & 0xffffffff;
  result[3] = (result[3] + d) & 0xffffffff;
}

static void md5_compute(const uint8_t *input, size_t length, uint8_t output[16]) {
  size_t i;
  size_t num_blocks = (length + 8) / 64 + 1;
  size_t padded_len = num_blocks * 64;
  uint8_t *padded = (uint8_t *)malloc(padded_len);
  
  if (!padded) return;
  
  memcpy(padded, input, length);
  padded[length] = 0x80;
  
  size_t bit_len = length * 8;
  memset(padded + length + 1, 0, padded_len - length - 9);
  memcpy(padded + padded_len - 8, &bit_len, 8);
  
  uint32_t result[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  
  for (i = 0; i < padded_len; i += 64) {
    uint32_t block[16];
    size_t j;
    for (j = 0; j < 16; j++) {
      block[j] = (uint32_t)padded[i + j * 4] |
                 ((uint32_t)padded[i + j * 4 + 1] << 8) |
                 ((uint32_t)padded[i + j * 4 + 2] << 16) |
                 ((uint32_t)padded[i + j * 4 + 3] << 24);
    }
    md5_process_block(result, block);
  }
  
  memcpy(output, result, 16);
  free(padded);
}

static int luaB_print (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  for (i = 1; i <= n; i++) {  /* for each argument */
    size_t l;
    const char *s = luaL_tolstring(L, i, &l);  /* convert it to string */
    if (i > 1)  /* not the first element? */
      lua_writestring("\t", 1);  /* add a tab before it */
    lua_writestring(s, l);  /* print it */
#ifdef __ANDROID__
      LOGD("%s", s);
#endif
    lua_pop(L, 1);  /* pop result */
  }
  lua_writeline();
  return 0;
}


/*
** Creates a warning with all given arguments.
** Check first for errors; otherwise an error may interrupt
** the composition of a warning, leaving it unfinished.
*/
static int luaB_warn (lua_State *L) {
  int n = lua_gettop(L);  /* number of arguments */
  int i;
  luaL_checkstring(L, 1);  /* at least one argument */
  for (i = 2; i <= n; i++)
    luaL_checkstring(L, i);  /* make sure all arguments are strings */
  for (i = 1; i < n; i++)  /* compose warning */
    lua_warning(L, lua_tostring(L, i), 1);
  lua_warning(L, lua_tostring(L, n), 0);  /* close warning */
  return 0;
}


#define SPACECHARS	" \f\n\r\t\v"

static const char *b_str2int (const char *s, unsigned base, lua_Integer *pn) {
  lua_Unsigned n = 0;
  int neg = 0;
  s += strspn(s, SPACECHARS);  /* skip initial spaces */
  if (*s == '-') { s++; neg = 1; }  /* handle sign */
  else if (*s == '+') s++;
  if (!isalnum(cast_uchar(*s)))  /* no digit? */
    return NULL;
  do {
    unsigned digit = cast_uint(isdigit(cast_uchar(*s))
                               ? *s - '0'
                               : (toupper(cast_uchar(*s)) - 'A') + 10);
    if (digit >= base) return NULL;  /* invalid numeral */
    n = n * base + digit;
    s++;
  } while (isalnum(cast_uchar(*s)));
  s += strspn(s, SPACECHARS);  /* skip trailing spaces */
  *pn = (lua_Integer)((neg) ? (0u - n) : n);
  return s;
}


static int luaB_tonumber (lua_State *L) {
  if (lua_isnoneornil(L, 2)) {  /* standard conversion? */
    if (lua_type(L, 1) == LUA_TNUMBER) {  /* already a number? */
      lua_settop(L, 1);  /* yes; return it */
      return 1;
    }
    else {
      size_t l;
      const char *s = lua_tolstring(L, 1, &l);
      if (s != NULL && lua_stringtonumber(L, s) == l + 1)
        return 1;  /* successful conversion to number */
      /* else not a number */
      luaL_checkany(L, 1);  /* (but there must be some parameter) */
    }
  }
  else {
    size_t l;
    const char *s;
    lua_Integer n = 0;  /* to avoid warnings */
    lua_Integer base = luaL_checkinteger(L, 2);
    luaL_checktype(L, 1, LUA_TSTRING);  /* no numbers as strings */
    s = lua_tolstring(L, 1, &l);
    luaL_argcheck(L, 2 <= base && base <= 36, 2, "base out of range");
    if (b_str2int(s, cast_uint(base), &n) == s + l) {
      lua_pushinteger(L, n);
      return 1;
    }  /* else not a number */
  }  /* else not a number */
  luaL_pushfail(L);  /* not a number */
  return 1;
}


static int luaB_tointeger (lua_State *L) {
    if (lua_type(L, 1) == LUA_TNUMBER){
        if (lua_isinteger(L, 1)) {
            lua_settop(L, 1);
            return 1;
        }
        else {
            lua_Number n = lua_tonumber(L, 1);
            lua_pushinteger(L, (lua_Integer)n);
            return 1;
        }
    }
    else if (lua_type(L, 1) == LUA_TBOOLEAN) {
        lua_pushinteger(L, lua_toboolean(L, 1) ? 1 : 0);
        return 1;
    }
    else {
        size_t l;
        const char *s = luaL_tolstring(L, 1, &l);
        if (s != NULL && lua_stringtonumber(L, s) == l + 1) {
            lua_Number n = lua_tonumber(L, 1);
            lua_pushinteger(L, (lua_Integer)n);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}


static int luaB_grand (lua_State *L) {
  lua_Number n = luaL_checknumber(L, 1);
  lua_pushinteger(L, (lua_Integer)(n >= 0 ? n + 0.5 : n - 0.5));
  return 1;
}


static int luaB_error (lua_State *L) {
  int level = (int)luaL_optinteger(L, 2, 1);
  lua_settop(L, 1);
  if (lua_type(L, 1) == LUA_TSTRING && level > 0) {
    luaL_where(L, level);   /* add extra information */
    lua_pushvalue(L, 1);
    lua_concat(L, 2);
  }
  return lua_error(L);
}


static int luaB_getmetatable (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    lua_pushnil(L);
    return 1;  /* no metatable */
  }
  luaL_getmetafield(L, 1, "__metatable");
  return 1;  /* returns either __metatable field (if present) or metatable */
}


static int luaB_setmetatable (lua_State *L) {
  int t = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
  
  if (l_unlikely(luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL))
    return luaL_error(L, "无法修改受保护的元表");
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}


static int luaB_rawequal (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_checkany(L, 2);
  lua_pushboolean(L, lua_rawequal(L, 1, 2));
  return 1;
}


static int luaB_rawlen (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argexpected(L, t == LUA_TTABLE || t == LUA_TSTRING, 1,
                      "table or string");
  lua_pushinteger(L, l_castU2S(lua_rawlen(L, 1)));
  return 1;
}


static int luaB_rawget (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  lua_settop(L, 2);
  lua_rawget(L, 1);
  return 1;
}

static int luaB_rawset (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkany(L, 2);
  luaL_checkany(L, 3);
  lua_settop(L, 3);
  lua_rawset(L, 1);
  return 1;
}


static int pushmode (lua_State *L, int oldmode) {
  if (oldmode == -1)
    luaL_pushfail(L);  /* invalid call to 'lua_gc' */
  else
    lua_pushstring(L, (oldmode == LUA_GCINC) ? "incremental"
                                             : "generational");
  return 1;
}


/*
** check whether call to 'lua_gc' was valid (not inside a finalizer)
*/
#define checkvalres(res) { if (res == -1) break; }

static int luaB_collectgarbage (lua_State *L) {
  static const char *const opts[] = {"stop", "restart", "collect",
    "count", "step", "setpause", "setstepmul",
    "isrunning", "generational", "incremental", "param",NULL};
  static const int optsnum[] = {LUA_GCSTOP, LUA_GCRESTART, LUA_GCCOLLECT,
    LUA_GCCOUNT, LUA_GCSTEP, LUA_GCSETPAUSE, LUA_GCSETSTEPMUL,
    LUA_GCISRUNNING, LUA_GCGEN, LUA_GCINC,LUA_GCPARAM};
  int o = optsnum[luaL_checkoption(L, 1, "collect", opts)];
  switch (o) {
    case LUA_GCCOUNT: {
      int k = lua_gc(L, o);
      int b = lua_gc(L, LUA_GCCOUNTB);
      checkvalres(k);
      lua_pushnumber(L, (lua_Number)k + ((lua_Number)b/1024));
      return 1;
    }
    case LUA_GCSTEP: {
      lua_Integer n = luaL_optinteger(L, 2, 0);
      int res = lua_gc(L, o, cast_sizet(n));
      checkvalres(res);
      lua_pushboolean(L, res);
      return 1;
    }
    case LUA_GCSETPAUSE:
    case LUA_GCSETSTEPMUL: {
      int p = (int)luaL_optinteger(L, 2, 0);
      int previous = lua_gc(L, o, p);
      checkvalres(previous);
      lua_pushinteger(L, previous);
      return 1;
    }
    case LUA_GCISRUNNING: {
      int res = lua_gc(L, o);
      checkvalres(res);
      lua_pushboolean(L, res);
      return 1;
    }
    case LUA_GCGEN: {
      int minormul = (int)luaL_optinteger(L, 2, 0);
      int majormul = (int)luaL_optinteger(L, 3, 0);
      return pushmode(L, lua_gc(L, o, minormul, majormul));
    }
    case LUA_GCINC: {
      int pause = (int)luaL_optinteger(L, 2, 0);
      int stepmul = (int)luaL_optinteger(L, 3, 0);
      int stepsize = (int)luaL_optinteger(L, 4, 0);
      return pushmode(L, lua_gc(L, o, pause, stepmul, stepsize));
    }
    case LUA_GCPARAM: {
      static const char *const params[] = {
        "minormul", "majorminor", "minormajor",
        "pause", "stepmul", "stepsize", NULL};
      static const char pnum[] = {
        LUA_GCPMINORMUL, LUA_GCPMAJORMINOR, LUA_GCPMINORMAJOR,
        LUA_GCPPAUSE, LUA_GCPSTEPMUL, LUA_GCPSTEPSIZE};
      int p = pnum[luaL_checkoption(L, 2, NULL, params)];
      lua_Integer value = luaL_optinteger(L, 3, -1);
      lua_pushinteger(L, lua_gc(L, o, p, (int)value));
      return 1;
    }
    default: {
      int res = lua_gc(L, o);
      checkvalres(res);
      lua_pushinteger(L, res);
      return 1;
    }
  }
  luaL_pushfail(L);  /* invalid call (inside a finalizer) */
  return 1;
}


static int luaB_isstruct (lua_State *L) {
  lua_pushboolean(L, lua_type(L, 1) == LUA_TSTRUCT);
  return 1;
}

static int luaB_isinstance (lua_State *L) {
  lua_pushboolean(L, luaC_instanceof(L, 1, 2));
  return 1;
}

static int luaB_type (lua_State *L) {
  int t = lua_type(L, 1);
  luaL_argcheck(L, t != LUA_TNONE, 1, "value expected");
  
  /* 检查元表是否有__type元方法 */
  if (lua_getmetatable(L, 1)) {
    lua_getfield(L, -1, "__type");
    if (lua_isfunction(L, -1)) {
      /* 调用__type元方法 */
      lua_pushvalue(L, 1);  /* 将原始值作为参数传入 */
      if (lua_pcall(L, 1, 1, 0) == 0) {
        /* 成功调用，使用返回的字符串类型 */
        if (lua_type(L, -1) == LUA_TSTRING) {
          lua_remove(L, -2);  /* 移除元表 */
          return 1;  /* 返回__type的返回值 */
        }
      }
      /* 如果调用失败或返回值不是字符串，恢复栈状态 */
      lua_pop(L, 2);  /* 移除__type函数和元表 */
    }
    else {
      lua_pop(L, 2);  /* 移除__type非函数值和元表 */
    }
  }
  
  lua_pushstring(L, lua_typename(L, t));
  return 1;
}


int luaB_next (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 2);  /* create a 2nd argument if there isn't one */
  if (lua_next(L, 1))
    return 2;
  else {
    lua_pushnil(L);
    return 1;
  }
}


static int pairscont (lua_State *L, int status, lua_KContext k) {
  (void)L; (void)status; (void)k;  /* unused */
  return 3;
}

static int luaB_pairs (lua_State *L) {
  luaL_checkany(L, 1);
  if (luaL_getmetafield(L, 1, "__pairs") == LUA_TNIL) {  /* no metamethod? */
    lua_pushcfunction(L, luaB_next);  /* will return generator, */
    lua_pushvalue(L, 1);  /* state, */
    lua_pushnil(L);  /* and initial value */
  }
  else {
    lua_pushvalue(L, 1);  /* argument 'self' to metamethod */
    lua_callk(L, 1, 3, 0, pairscont);  /* get 3 values from metamethod */
  }
  return 3;
}


/*
** Traversal function for 'ipairs'
*/
static int ipairsaux (lua_State *L) {
  lua_Integer i = luaL_checkinteger(L, 2);
  i = luaL_intop(+, i, 1);
  lua_pushinteger(L, i);
  return (lua_geti(L, 1, i) == LUA_TNIL) ? 1 : 2;
}


/*
** 'ipairs' function. Returns 'ipairsaux', given "table", 0.
** (The given "table" may not be a table.)
*/
static int luaB_ipairs (lua_State *L) {
  luaL_checkany(L, 1);
  lua_pushcfunction(L, ipairsaux);  /* iteration function */
  lua_pushvalue(L, 1);  /* state */
  lua_pushinteger(L, 0);  /* initial value */
  return 3;
}

static int load_aux (lua_State *L, int status, int envidx) {
  if (l_likely(status == LUA_OK)) {
    if (envidx != 0) {  /* 'env' parameter? */
      lua_pushvalue(L, envidx);  /* environment for loaded function */
      if (!lua_setupvalue(L, -2, 1))  /* set it as 1st upvalue */
        lua_pop(L, 1);  /* remove 'env' if not used by previous call */
    }
    return 1;
  }
  else {  /* error (message is on top of the stack) */
    luaL_pushfail(L);
    lua_insert(L, -2);  /* put before error message */
    return 2;  /* return fail plus error message */
  }
}


static const char *getMode (lua_State *L, int idx) {
  const char *mode = luaL_optstring(L, idx, "bt");
  if (strchr(mode, 'B') != NULL)  /* Lua code cannot use fixed buffers */
    luaL_argerror(L, idx, "invalid mode");
  return mode;
}


static int luaB_loadfile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  const char *mode = luaL_optstring(L, 2, NULL);
  int env = (!lua_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = luaL_loadfilex(L, fname, mode);
  return load_aux(L, status, env);
}

static int luaB_loadsfile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  const char *mode = luaL_optstring(L, 2, NULL);
  char new_mode[16];
  if (mode) {
    if (strlen(mode) > 10) return luaL_error(L, "mode string too long");
    snprintf(new_mode, sizeof(new_mode), "%sS", mode);
  } else {
    strcpy(new_mode, "btS");
  }
  int env = (!lua_isnone(L, 3) ? 3 : 0);  /* 'env' index or 0 if no 'env' */
  int status = luaL_loadfilex(L, fname, new_mode);
  return load_aux(L, status, env);
}


/*
** {===========================================
** Generic Read function
** ============================================
*/


/*
** reserved slot, above all arguments, to hold a copy of the returned
** string to avoid it being collected while parsed. 'load' has four
** optional arguments (chunk, source name, mode, and environment).
*/
#define RESERVEDSLOT	5


/*
** Reader for generic 'load' function: 'lua_load' uses the
** stack for internal stuff, so the reader cannot change the
** stack top. Instead, it keeps its resulting string in a
** reserved slot inside the stack.
*/
static const char *generic_reader (lua_State *L, void *ud, size_t *size) {
  (void)(ud);  /* not used */
  luaL_checkstack(L, 2, "too many nested functions");
  lua_pushvalue(L, 1);  /* get function */
  lua_call(L, 0, 1);  /* call it */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* pop result */
    *size = 0;
    return NULL;
  }
  else if (l_unlikely(!lua_isstring(L, -1)))
    luaL_error(L, "reader function must return a string");
  lua_replace(L, RESERVEDSLOT);  /* save string in reserved slot */
  return lua_tolstring(L, RESERVEDSLOT, size);
}


static int luaB_load (lua_State *L) {
  int status;
  size_t l;
  const char *s = lua_tolstring(L, 1, &l);
  const char *mode = luaL_optstring(L, 3, "bt");
  int env = (!lua_isnone(L, 4) ? 4 : 0);  /* 'env' index or 0 if no 'env' */
  if (s != NULL) {  /* loading a string? */
    const char *chunkname = luaL_optstring(L, 2, s);
    status = luaL_loadbufferx(L, s, l, chunkname, mode);
  }
  else {  /* loading from a reader function */
    const char *chunkname = luaL_optstring(L, 2, "=(load)");
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_settop(L, RESERVEDSLOT);  /* create reserved slot */
    status = lua_load(L, generic_reader, NULL, chunkname, mode);
  }
  return load_aux(L, status, env);
}

/* }=========================================== */


static int dofilecont (lua_State *L, int d1, lua_KContext d2) {
  (void)d1;  (void)d2;  /* only to match 'lua_Kfunction' prototype */
  return lua_gettop(L) - 1;
}


static int luaB_dofile (lua_State *L) {
  const char *fname = luaL_optstring(L, 1, NULL);
  lua_settop(L, 1);
  if (l_unlikely(luaL_loadfile(L, fname) != LUA_OK))
    return lua_error(L);
  lua_callk(L, 0, LUA_MULTRET, 0, dofilecont);
  return dofilecont(L, 0, 0);
}


static int luaB_assert (lua_State *L) {
  if (l_likely(lua_toboolean(L, 1)))  /* condition is true? */
    return lua_gettop(L);  /* return all arguments */
  else {  /* error */
    luaL_checkany(L, 1);  /* there must be a condition */
    lua_remove(L, 1);  /* remove it */
    lua_pushliteral(L, "assertion failed!");  /* default message */
    lua_settop(L, 1);  /* leave only message (default if no other one) */
    return luaB_error(L);  /* call 'error' */
  }
}


static int luaB_select (lua_State *L) {
  int n = lua_gettop(L);
  if (lua_type(L, 1) == LUA_TSTRING && *lua_tostring(L, 1) == '#') {
    lua_pushinteger(L, n-1);
    return 1;
  }
  else {
    lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 0) i = n + i;
    else if (i > n) i = n;
    luaL_argcheck(L, 1 <= i, 1, "index out of range");
    return n - (int)i;
  }
}


/*
** Continuation function for 'pcall' and 'xpcall'. Both functions
** already pushed a 'true' before doing the call, so in case of success
** 'finishpcall' only has to return everything in the stack minus
** 'extra' values (where 'extra' is exactly the number of items to be
** ignored).
*/
static int finishpcall (lua_State *L, int status, lua_KContext extra) {
  if (l_unlikely(status != LUA_OK && status != LUA_YIELD)) {  /* error? */
    lua_pushboolean(L, 0);  /* first result (false) */
    lua_pushvalue(L, -2);  /* error message */
    return 2;  /* return false, msg */
  }
  else
    return lua_gettop(L) - (int)extra;  /* return all results */
}


static int luaB_pcall (lua_State *L) {
  int status;
  luaL_checkany(L, 1);
  lua_pushboolean(L, 1);  /* first result if no errors */
  lua_insert(L, 1);  /* put it in place */
  status = lua_pcallk(L, lua_gettop(L) - 2, LUA_MULTRET, 0, 0, finishpcall);
  return finishpcall(L, status, 0);
}


/*
** Do a protected call with error handling. After 'lua_rotate', the
** stack will have <f, err, true, f, [args...]>; so, the function passes
** 2 to 'finishpcall' to skip the 2 first values when returning results.
*/
static int luaB_xpcall (lua_State *L) {
  int status;
  int n = lua_gettop(L);
  luaL_checktype(L, 2, LUA_TFUNCTION);  /* check error function */
  lua_pushboolean(L, 1);  /* first result */
  lua_pushvalue(L, 1);  /* function */
  lua_rotate(L, 3, 2);  /* move them below function's arguments */
  status = lua_pcallk(L, n - 2, LUA_MULTRET, 2, 2, finishpcall);
  return finishpcall(L, status, 2);
}


static int luaB_tostring (lua_State *L) {
  luaL_checkany(L, 1);
  luaL_tolstring(L, 1, NULL);
  return 1;
}


/* compatibility with old module system */
#if defined(LUA_COMPAT_MODULE)
static int findtable (lua_State *L) {
  if (lua_gettop(L)==1){
    lua_pushglobaltable(L);
    lua_insert(L, 1);
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  const char *name = luaL_checklstring(L, 2, 0);
  lua_pushstring(L, luaL_findtable(L, 1, name, 0));
  return 2;
}
#endif


/* base64 encoding support */
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(lua_State *L, const char *input, size_t in_len, char **output, size_t *out_len) {
  size_t i, j;
  *out_len = ((in_len + 2) / 3) * 4;
  *output = (char *)lua_newuserdatauv(L, *out_len + 1, 0);
  
  for (i = 0, j = 0; i < in_len; i += 3, j += 4) {
    uint32_t val = 0;
    int k, count = 0;
    for (k = 0; k < 3 && (i + k) < in_len; k++) {
      val = (val << 8) | (unsigned char)input[i + k];
      count++;
    }
    
    // 计算每个base64字符
    // 注意：位移计算应基于实际的count值
    switch (count) {
      case 3:
        (*output)[j] = b64chars[(val >> 18) & 0x3F];
        (*output)[j + 1] = b64chars[(val >> 12) & 0x3F];
        (*output)[j + 2] = b64chars[(val >> 6) & 0x3F];
        (*output)[j + 3] = b64chars[val & 0x3F];
        break;
      case 2:
        (*output)[j] = b64chars[(val >> 10) & 0x3F];
        (*output)[j + 1] = b64chars[(val >> 4) & 0x3F];
        (*output)[j + 2] = b64chars[((val << 2) & 0x3F)];
        (*output)[j + 3] = '=';
        break;
      case 1:
        (*output)[j] = b64chars[(val >> 2) & 0x3F];
        (*output)[j + 1] = b64chars[((val << 4) & 0x3F)];
        (*output)[j + 2] = '=';
        (*output)[j + 3] = '=';
        break;
    }
  }
  
  (*output)[*out_len] = '\0';
}

/* simple XOR encryption */
static void xor_encrypt(const char *input, size_t in_len, char *output, char key) {
  size_t i;
  for (i = 0; i < in_len; i++) {
    output[i] = input[i] ^ key;
  }
}

/* 递归格式化表为字符串 - 完全按照import.lua的dump函数逻辑实现 */
/* 辅助结构：已访问表记录，用表的指针地址作为key */
typedef struct {
  lua_Integer count;
  const void **tables;  /* 存储表指针 */
  char **paths;         /* 存储对应的路径字符串 */
} VisitedTables;

/* 检查表是否已访问过，如果没访问过则添加到列表 */
static const char* check_and_add_visited(VisitedTables *vt, const void *table_addr, const char *path) {
  /* 检查是否已访问 */
  for (lua_Integer i = 0; i < vt->count; i++) {
    if (vt->tables[i] == table_addr) {
      return vt->paths[i];  /* 返回之前的路径 */
    }
  }
  
  /* 添加到已访问列表 */
  const void **new_tables = (const void **)realloc(vt->tables, sizeof(void *) * (vt->count + 1));
  char **new_paths = (char **)realloc(vt->paths, sizeof(char *) * (vt->count + 1));
  if (new_tables == NULL || new_paths == NULL) {
    return "<?>";  /* 内存错误时返回占位符 */
  }
  vt->tables = new_tables;
  vt->paths = new_paths;
  vt->tables[vt->count] = table_addr;
  vt->paths[vt->count] = path ? strdup(path) : strdup("");
  vt->count++;
  
  return NULL;  /* 返回NULL表示是新表 */
}

/* 重置已访问表记录 */
static void reset_visited_tables(VisitedTables *vt) {
  for (lua_Integer i = 0; i < vt->count; i++) {
    if (vt->paths[i]) free(vt->paths[i]);
  }
  if (vt->tables) free((void*)vt->tables);
  if (vt->paths) free(vt->paths);
  vt->tables = NULL;
  vt->paths = NULL;
  vt->count = 0;
}

/* 辅助函数：检查值是否等于_G */
static int is_value_equal_G(lua_State *L, int value_idx) {
  value_idx = lua_absindex(L, value_idx);
  lua_pushglobaltable(L);
  int equal = lua_rawequal(L, -1, value_idx);
  lua_pop(L, 1);
  return equal;
}

/* 辅助函数：检查值是否等于package.loaded */
static int is_value_equal_package_loaded(lua_State *L, int value_idx) {
  value_idx = lua_absindex(L, value_idx);
  lua_getglobal(L, "package");
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return 0;
  }
  lua_getfield(L, -1, "loaded");
  int equal = lua_rawequal(L, -1, value_idx);
  lua_pop(L, 2);
  return equal;
}

static void format_table(lua_State *L, int idx, luaL_Buffer *buffer, int indent, int depth, VisitedTables *visited, const char *current_path) {
  int i;
  
  /* 将相对索引转换为绝对索引 */
  idx = lua_absindex(L, idx);
  
  /* 检查元表是否有__tostring方法 */
  if (lua_getmetatable(L, idx)) {
    lua_getfield(L, -1, "__tostring");
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, idx);
      if (lua_pcall(L, 1, 1, 0) == 0) {
        const char *str = lua_tostring(L, -1);
        if (str != NULL) {
          luaL_addstring(buffer, str);
          lua_pop(L, 2);  /* pop result and metatable */
          return;
        }
      }
      lua_pop(L, 1);
    } else {
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
  }
  
  luaL_addstring(buffer, "{");
  
  /* 限制递归深度 */
  if (depth > 20) {
    luaL_addstring(buffer, "...}");
    return;
  }
  
  int first = 1;
  lua_pushnil(L);
  while (lua_next(L, idx) != 0) {
    /* 栈: ... key value */
    int value_idx = lua_absindex(L, -1);
    int key_idx = lua_absindex(L, -2);
    
    /* 检查值是否是_G */
    if (lua_istable(L, value_idx) && is_value_equal_G(L, value_idx)) {
      /* 值是_G，输出 key = _G */
      if (!first) luaL_addstring(buffer, ",");
      first = 0;
      luaL_addstring(buffer, "\n");
      for (i = 0; i < indent + 2; i++) luaL_addchar(buffer, ' ');
      
      /* 输出键 */
      lua_pushvalue(L, key_idx);
      if (lua_type(L, -1) == LUA_TSTRING) {
        luaL_addstring(buffer, "[\"");
        luaL_addstring(buffer, lua_tostring(L, -1));
        luaL_addstring(buffer, "\"]");
      } else {
        lua_pushfstring(L, "[%d]", (int)lua_tointeger(L, -1));
        luaL_addvalue(buffer);
      }
      lua_pop(L, 1);
      
      luaL_addstring(buffer, " = _G");
      lua_pop(L, 1);  /* pop value, keep key for next iteration */
      continue;
    }
    
    /* 检查值是否是package.loaded，如果是则跳过 */
    if (lua_istable(L, value_idx) && is_value_equal_package_loaded(L, value_idx)) {
      lua_pop(L, 1);  /* pop value, keep key for next iteration */
      continue;
    }
    
    if (!first) luaL_addstring(buffer, ",");
    first = 0;
    luaL_addstring(buffer, "\n");
    for (i = 0; i < indent + 2; i++) luaL_addchar(buffer, ' ');
    
    /* 构建键的字符串表示，用于路径 */
    char key_str[64] = "";
    lua_pushvalue(L, key_idx);
    if (lua_type(L, -1) == LUA_TSTRING) {
      snprintf(key_str, sizeof(key_str), "%s", lua_tostring(L, -1));
      luaL_addstring(buffer, "[\"");
      luaL_addstring(buffer, key_str);
      luaL_addstring(buffer, "\"]");
    } else if (lua_type(L, -1) == LUA_TNUMBER) {
      snprintf(key_str, sizeof(key_str), "%d", (int)lua_tointeger(L, -1));
      lua_pushfstring(L, "[%s]", key_str);
      luaL_addvalue(buffer);
    } else {
      luaL_addstring(buffer, "[");
      luaL_tolstring(L, -1, NULL);
      luaL_addvalue(buffer);
      luaL_addstring(buffer, "]");
      snprintf(key_str, sizeof(key_str), "?");
    }
    lua_pop(L, 1);
    
    luaL_addstring(buffer, " = ");
    
    /* 格式化值 */
    int vt = lua_type(L, value_idx);
    if (vt == LUA_TNUMBER) {
      lua_pushvalue(L, value_idx);
      luaL_addstring(buffer, lua_tostring(L, -1));
      lua_pop(L, 1);
    } else if (vt == LUA_TSTRING) {
      const char *s = lua_tostring(L, value_idx);
      luaL_addstring(buffer, "\"");
      if (s && strlen(s) > 100) {
        char truncated[104];
        strncpy(truncated, s, 100);
        truncated[100] = '\0';
        luaL_addstring(buffer, truncated);
        luaL_addstring(buffer, "...");
      } else {
        luaL_addstring(buffer, s ? s : "");
      }
      luaL_addstring(buffer, "\"");
    } else if (vt == LUA_TTABLE) {
      /* 检查循环引用 */
      const void *tbl_ptr = lua_topointer(L, value_idx);
      char new_path[256];
      snprintf(new_path, sizeof(new_path), "%s%s", current_path ? current_path : "", key_str);
      
      const char *prev_path = check_and_add_visited(visited, tbl_ptr, new_path);
      if (prev_path) {
        /* 已访问过，输出路径引用 */
        luaL_addstring(buffer, prev_path);
      } else {
        /* 递归处理 */
        format_table(L, value_idx, buffer, indent + 2, depth + 1, visited, new_path);
      }
    } else if (vt == LUA_TBOOLEAN) {
      luaL_addstring(buffer, lua_toboolean(L, value_idx) ? "true" : "false");
    } else if (vt == LUA_TFUNCTION) {
      luaL_addstring(buffer, "<function>");
    } else if (vt == LUA_TUSERDATA) {
      luaL_addstring(buffer, "<userdata>");
    } else if (vt == LUA_TTHREAD) {
      luaL_addstring(buffer, "<thread>");
    } else if (vt == LUA_TLIGHTUSERDATA) {
      luaL_addstring(buffer, "<lightuserdata>");
    } else if (vt == LUA_TNIL) {
      luaL_addstring(buffer, "nil");
    } else {
      luaL_addstring(buffer, "<unknown>");
    }
    
    lua_pop(L, 1);  /* pop value, keep key for next iteration */
  }
  
  if (!first) {
    luaL_addstring(buffer, "\n");
    for (i = 0; i < indent; i++) luaL_addchar(buffer, ' ');
  }
  luaL_addstring(buffer, "}");
}

/* base64解码 */
static void base64_decode(lua_State *L, const char *input, size_t in_len, char **output, size_t *out_len) {
  static const unsigned char b64map[] = {
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 62, 255, 255, 255, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255, 255, 255, 254, 255, 255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 255, 255, 255, 255, 255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
  };
  
  size_t i, j;
  
  // 跳过所有无效字符，只处理有效的base64字符
  size_t valid_chars = 0;
  size_t padding = 0;
  
  // 第一次遍历：计算有效字符数和填充字符数
  for (i = 0; i < in_len; i++) {
    unsigned char c = input[i];
    unsigned char b = b64map[c];
    if (b != 255) {
      valid_chars++;
      if (b == 254) { // 填充字符 '='
        padding++;
      }
    }
  }
  
  // 计算输出长度
  *out_len = ((valid_chars / 4) * 3) - padding;
  if (*out_len == 0 && valid_chars > 0) {
    // 特殊处理：当有有效字符但计算出的输出长度为0时，如单个字符加密后的情况
    *out_len = 1;
  }
  
  *output = (char *)lua_newuserdatauv(L, *out_len, 0);
  
  uint32_t val = 0;
  int bits = 0;
  j = 0;
  
  // 第二次遍历：实际解码
  for (i = 0; i < in_len && j < *out_len; i++) {
    unsigned char c = input[i];
    unsigned char b = b64map[c];
    
    if (b == 255) {
      // 跳过无效字符
      continue;
    }
    
    if (b == 254) { // 填充字符 '='
      // 对于填充字符，我们继续处理，直到获取足够的位
      val = (val << 6);
      bits += 6;
    } else {
      val = (val << 6) | b;
      bits += 6;
    }
    
    if (bits >= 8) {
      bits -= 8;
      (*output)[j++] = (val >> bits) & 0xFF;
    }
  }
}

static int luaB_dump (lua_State *L) {
  int t = lua_type(L, 1);
  
  // 检查是否有第二个参数
  if (lua_gettop(L) == 2 && t == LUA_TSTRING) {
    const char *str = lua_tostring(L, 1);
    
    if (lua_isboolean(L, 2)) {
      // 第二个参数是布尔值
      int decrypt = lua_toboolean(L, 2);
      if (decrypt) {
        // 解密：base64解码 + XOR解密
        size_t str_len = lua_rawlen(L, 1);
        
        // base64解码
        char *decoded;
        size_t decoded_len;
        base64_decode(L, str, str_len, &decoded, &decoded_len);
        
        // XOR解密
        char *result = (char *)lua_newuserdatauv(L, decoded_len, 0);
        xor_encrypt(decoded, decoded_len, result, 0x5A); // 使用相同密钥解密
        
        // 压入结果
        lua_pushlstring(L, result, decoded_len);
        return 1;
      } else {
        // 传入false，返回nil
        lua_pushnil(L);
        return 1;
      }
    } else if (lua_isfunction(L, 2)) {
      // 第二个参数是函数，调用该函数并传入第一个参数
      lua_pushvalue(L, 2); // 函数
      lua_pushvalue(L, 1); // 第一个参数
      if (lua_pcall(L, 1, 1, 0) != 0) {
        // 调用出错，返回错误信息
        return 1;
      }
      return 1;
    }
  }
  
  // 处理单个参数的情况
  switch (t) {
    case LUA_TTABLE: {
      // 把表格式化转字符串
      luaL_Buffer buffer;
      luaL_buffinit(L, &buffer);
      
      /* 初始化已访问表记录 */
      VisitedTables visited;
      visited.count = 0;
      visited.tables = NULL;
      visited.paths = NULL;
      
      /* 格式化表 */
      format_table(L, 1, &buffer, 0, 0, &visited, "");
      
      /* 清理已访问表记录 */
      reset_visited_tables(&visited);
      
      luaL_pushresult(&buffer);
      return 1;
    }
    case LUA_TSTRING: {
      // 编译成加密文本然后用base64包裹
      size_t str_len;
      const char *str = lua_tolstring(L, 1, &str_len);
      
      // XOR加密
      char *encrypted = (char *)lua_newuserdatauv(L, str_len, 0);
      xor_encrypt(str, str_len, encrypted, 0x5A); // 使用0x5A作为密钥
      
      // base64编码
      char *encoded;
      size_t encoded_len;
      base64_encode(L, encrypted, str_len, &encoded, &encoded_len);
      
      // 压入结果
      lua_pushlstring(L, encoded, encoded_len);
      return 1;
    }
    case LUA_TUSERDATA: {
      // 尝试强制转化为string
      luaL_tolstring(L, 1, NULL);
      return 1;
    }
    default: {
      // 其他类型直接转化为字符串
      luaL_tolstring(L, 1, NULL);
      return 1;
    }
  }
}

// __gc元方法的回调函数
static int defer_gc_callback (lua_State *L) {
  lua_getiuservalue(L, 1, 1);
  lua_call(L, 0, 0);
  return 0;
}

static int luaB_defer (lua_State *L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  
  // 创建一个带有__gc元方法的用户数据
  lua_newuserdatauv(L, 0, 1);
  lua_pushvalue(L, 1);
  lua_setiuservalue(L, -2, 1);
  
  // 创建元表
  lua_createtable(L, 0, 1);
  
  // 定义__gc元方法
  lua_pushcfunction(L, defer_gc_callback);
  lua_setfield(L, -2, "__gc");
  
  // 设置元表
  lua_setmetatable(L, -2);
  
  return 0;
}

// 模块信息结构体
typedef struct {
  const char *name;  // 模块名
  lua_CFunction init;  // 模块初始化函数
} ModuleInfo;

// 标准库模块列表
static const ModuleInfo modules[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_IOLIBNAME, luaopen_io},
  {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_UTF8LIBNAME, luaopen_utf8},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_BOOLIBNAME, luaopen_bool},
  {LUA_UDATALIBNAME, luaopen_userdata},
  {LUA_VMLIBNAME, luaopen_vm},
  {LUA_DBLIBNAME, luaopen_debug},
  {LUA_BITLIBNAME, luaopen_bit},
  {LUA_PTRLIBNAME, luaopen_ptr},

#ifndef _WIN32
  {LUA_SMGRNAME, luaopen_smgr},
  {"translator", luaopen_translator},

  // 仅安卓端（Android）才加 libc 模块
#ifdef __ANDROID__
  {"libc", luaopen_libc},
#endif

#endif

  {NULL, NULL}
};


// 前置声明
static int luaB_fsleep (lua_State *L);
static int luaB_fwake (lua_State *L);
static int luaB_grand (lua_State *L);
static int luaB_md5 (lua_State *L);

// 基本库函数列表
static const luaL_Reg env_funcs[] = {
  {"assert", luaB_assert},
  {"collectgarbage", luaB_collectgarbage},
  {"defer", luaB_defer},
  {"dofile", luaB_dofile},
  {"dump", luaB_dump},
  {"error", luaB_error},
  {"grand", luaB_grand},
  {"fsleep", luaB_fsleep},
  {"getmetatable", luaB_getmetatable},
  {"ipairs", luaB_ipairs},
  {"loadfile", luaB_loadfile},
  {"loadsfile", luaB_loadsfile},
  {"load", luaB_load},
  {"loadstring", luaB_load},
  {"next", luaB_next},
  {"pairs", luaB_pairs},
  {"pcall", luaB_pcall},
  {"wymd5", luaB_md5},
  {"print", luaB_print},
  {"warn", luaB_warn},
  {"rawequal", luaB_rawequal},
  {"rawlen", luaB_rawlen},
  {"rawget", luaB_rawget},
  {"rawset", luaB_rawset},
  {"select", luaB_select},
  {"setmetatable", luaB_setmetatable},
  {"tonumber", luaB_tonumber},
  {"tointeger", luaB_tointeger},
  {"tostring", luaB_tostring},
  {"type", luaB_type},
  {"xpcall", luaB_xpcall},
  {NULL, NULL}
};

// C函数包装器的调用元方法
static int cfunction_wrapper_call(lua_State *L) {
  // 获取存储的C函数指针
  lua_CFunction f = (lua_CFunction)lua_touserdata(L, lua_upvalueindex(1));
  
  // 调用C函数，传递所有参数
  return f(L);
}

// 保护函数表的元方法
static int protected_table_newindex (lua_State *L) {
  return luaL_error(L, "cannot modify protected function table");
}

static int luaB_getfenv (lua_State *L) {
  if (lua_isnoneornil(L, 1)) {
    // 没有参数，返回当前函数的环境
    lua_getglobal(L, "_ENV");
    return 1;
  } else {
    // 有参数，返回指定函数或线程的环境
    int type = lua_type(L, 1);
    if (type == LUA_TFUNCTION || type == LUA_TTHREAD) {
      // 获取函数或线程的环境
      lua_getuservalue(L, 1);
      if (lua_isnil(L, -1)) {
        // 如果没有环境，返回全局环境
        lua_pop(L, 1);
        lua_getglobal(L, "_ENV");
      }
      return 1;
    } else if (type == LUA_TNUMBER) {
      // 旧版Lua 5.1中，getfenv(1)获取当前函数的环境
      // 这里模拟这个行为
      lua_getglobal(L, "_ENV");
      return 1;
    } else {
      return luaL_error(L, "错误的参数 #1 传递给 'getfenv' (需要函数、线程或数字)");
    }
  }
}

static int luaB_setfenv (lua_State *L) {
  int type = lua_type(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  
  if (type == LUA_TFUNCTION || type == LUA_TTHREAD) {
    // 设置函数或线程的环境
    lua_setuservalue(L, 1);
    lua_pushvalue(L, 2);  // 返回新的环境
    return 1;
  } else if (type == LUA_TNUMBER) {
    // 旧版Lua 5.1中，setfenv(1, table)设置当前函数的环境
    // 这里模拟这个行为，实际上是修改全局_ENV
    lua_setglobal(L, "_ENV");
    lua_pushvalue(L, 2);  // 返回新的环境
    return 1;
  } else {
    return luaL_error(L, "bad argument #1 to 'setfenv' (function, thread or number expected)");
  }
}

static int luaB_getenv_original (lua_State *L) {
  if (lua_isnoneornil(L, 1)) {
    // 没有参数，返回整个函数表
    lua_createtable(L, 0, 50);  // 创建一个足够大的表
    
    // 遍历所有基本库函数
    for (int i = 0; env_funcs[i].name != NULL; i++) {
      if (env_funcs[i].func != NULL) {
        // 直接将C函数作为值设置到表中
        lua_pushcfunction(L, env_funcs[i].func);
        lua_setfield(L, -2, env_funcs[i].name);  // 将函数放入表中
      }
    }
    
    // 遍历所有模块
    for (int i = 0; modules[i].name != NULL; i++) {
      if (modules[i].init != NULL && strcmp(modules[i].name, LUA_GNAME) != 0) {
        // 初始化模块
        modules[i].init(L);
        // 将模块设置到函数表中
        lua_setfield(L, -2, modules[i].name);
      }
    }
    
    // 设置元表保护
    lua_createtable(L, 0, 1);  // 创建元表
    lua_pushcfunction(L, protected_table_newindex);  // __newindex元方法
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "protected table");  // 设置__metatable，防止获取元表
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);  // 设置表的元表
    
    return 1;
  } else {
    // 有参数，返回单个函数或模块
    const char *funcname = luaL_checkstring(L, 1);
    
    // 1. 直接查找基本库函数
    for (int i = 0; env_funcs[i].name != NULL; i++) {
      if (strcmp(env_funcs[i].name, funcname) == 0 && env_funcs[i].func != NULL) {
        // 直接返回C函数
        lua_pushcfunction(L, env_funcs[i].func);
        return 1;
      }
    }
    
    // 2. 直接查找模块
    for (int i = 0; modules[i].name != NULL; i++) {
      if (strcmp(modules[i].name, funcname) == 0 && modules[i].init != NULL) {
        // 初始化模块并返回
        modules[i].init(L);
        return 1;
      }
    }
    
    // 3. 如果没找到，返回nil
    lua_pushnil(L);
    return 1;
  }
}

/*
** 将字符串转换为ASCII字符的十六进制转义格式
** 例如: "Hello" -> "\x48\x65\x6C\x6C\x6F"
*/
static int luaB_toasc2i (lua_State *L) {
  size_t len;
  const char *str = luaL_checklstring(L, 1, &len);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  
  for (size_t i = 0; i < len; i++) {
    char hex_str[5]; // "\x" + 2 hex digits + null terminator
    sprintf(hex_str, "\\x%02X", (unsigned char)str[i]);
    luaL_addstring(&b, hex_str);
  }
  
  luaL_pushresult(&b);
  return 1;
}


/*
** match(t) - 高性能字符串替换函数
** 参数：表t，最多4个元素
** t[1]: 原始字符串
** t[2]: 要查找的字符串1（可选）
** t[3]: 要替换的字符串1（可选）
** t[4]: 要查找的字符串2 或 布尔值（是否开启正则）
** 
** 功能：依次用后面的字符串替换前面的字符串
** 示例：match({"我真的爱你我爱你", "我爱你", "行"}) 返回 "我真的爱你行"
** 如果不存在匹配项则不进行替换
** 
** 使用Sunday算法进行高效字符串查找（比Boyer-Moore更快）
** 混合策略：小模式串使用优化算法，大模式串使用Sunday
*/

static const unsigned char bm_ascii[256] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
  32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
  64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
  80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
  96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
  112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127,
  128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
  144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159,
  160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
  176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
  192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
  208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
  224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
  240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255
};

static void bm_preprocess_badchar(const char *pattern, size_t pat_len, int *badchar) {
  size_t i;
  for (i = 0; i < 256; i++) {
    badchar[i] = (int)pat_len;
  }
  for (i = 0; i < pat_len; i++) {
    badchar[(unsigned char)pattern[i]] = (int)(pat_len - i - 1);
  }
}

static const char *bm_search(const char *text, size_t text_len, const char *pattern, size_t pat_len, const int *badchar) {
  if (pat_len == 0 || text_len < pat_len) {
    return NULL;
  }
  
  size_t shift = 0;
  while (shift <= text_len - pat_len) {
    size_t j = pat_len - 1;
    while (j > 0 && pattern[j] == text[shift + j]) {
      j--;
    }
    if (pattern[0] == text[shift + j] || j == 0) {
      if (pat_len == 1 || memcmp(pattern, text + shift, pat_len) == 0) {
        return text + shift;
      }
    }
    shift += badchar[(unsigned char)text[shift + j]];
  }
  
  return NULL;
}

static void sunday_preprocess(const char *pattern, size_t pat_len, int *shift) {
  size_t i;
  for (i = 0; i < 256; i++) {
    shift[i] = (int)pat_len + 1;
  }
  for (i = 0; i < pat_len; i++) {
    shift[(unsigned char)pattern[i]] = (int)(pat_len - i);
  }
}

static const char *sunday_search(const char *text, size_t text_len, const char *pattern, size_t pat_len, const int *shift) {
  if (pat_len == 0 || text_len < pat_len) {
    return NULL;
  }
  
  size_t pos = 0;
  while (pos <= text_len - pat_len) {
    if (memcmp(pattern, text + pos, pat_len) == 0) {
      return text + pos;
    }
    if (pos + pat_len < text_len) {
      pos += shift[(unsigned char)text[pos + pat_len]];
    } else {
      break;
    }
  }
  
  return NULL;
}

static inline const char *fast_search(const char *text, size_t text_len, const char *pattern, size_t pat_len, int *work_buf) {
  if (pat_len == 0 || text_len < pat_len) {
    return NULL;
  }
  
  if (pat_len <= 4) {
    uint32_t mask = 0;
    const unsigned char *p = (const unsigned char *)pattern;
    size_t i;
    for (i = 0; i < pat_len; i++) {
      mask = (mask << 8) | p[i];
    }
    mask <<= (4 - pat_len) * 8;
    
    const unsigned char *t = (const unsigned char *)text;
    const unsigned char *end = t + text_len - pat_len + 1;
    
    while (t < end) {
      uint32_t chunk = *(uint32_t *)t;
      if ((chunk & mask) == mask) {
        size_t j = 0;
        while (j < pat_len && t[j] == p[j]) {
          j++;
        }
        if (j == pat_len) {
          return (const char *)t;
        }
      }
      t++;
    }
    return NULL;
  }
  
  if (pat_len <= 16) {
    sunday_preprocess(pattern, pat_len, work_buf);
    return sunday_search(text, text_len, pattern, pat_len, work_buf);
  }
  
  bm_preprocess_badchar(pattern, pat_len, work_buf);
  return bm_search(text, text_len, pattern, pat_len, work_buf);
}

static const char *match_regex(const char *src, size_t src_len, const char *pattern, size_t *match_len) {
  const char *p = pattern;
  const char *s = src;
  const char *end = src + src_len;
  const char *wildcard = NULL;
  const char *wildcard_end = NULL;
  
  while (p < pattern + strlen(pattern) && s < end) {
    if (*p == '*') {
      wildcard = p;
      wildcard_end = s;
      p++;
    } else if (*p == '?' || *p == *s) {
      p++;
      s++;
    } else if (wildcard) {
      p = wildcard + 1;
      wildcard_end++;
      s = wildcard_end;
    } else {
      return NULL;
    }
  }
  
  while (*p == '*') p++;
  
  if (*p == '\0' && (s == end || *(p - 1) == '*')) {
    *match_len = s - src;
    return src;
  }
  
  return NULL;
}

static int luaB_match (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  
  int n = lua_rawlen(L, 1);
  if (n < 1) {
    lua_pushnil(L);
    return 1;
  }
  
  lua_geti(L, 1, 1);
  if (lua_type(L, -1) != LUA_TSTRING) {
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
  }
  
  const char *src = lua_tostring(L, -1);
  size_t src_len = strlen(src);
  
  int use_regex = 0;
  int max_pairs = n - 1;
  
  if (max_pairs >= 1) {
    lua_geti(L, 1, n);
    if (lua_type(L, -1) == LUA_TBOOLEAN) {
      use_regex = lua_toboolean(L, -1);
      max_pairs--;
    }
    lua_pop(L, 1);
  }
  
  if (max_pairs == 0) {
    lua_pushstring(L, src);
    return 1;
  }
  
  const char *find_str = NULL;
  const char *replace_str = NULL;
  
  lua_geti(L, 1, 2);
  if (lua_type(L, -1) == LUA_TSTRING) {
    find_str = lua_tostring(L, -1);
  }
  
  if (max_pairs >= 1) {
    lua_geti(L, 1, 3);
    if (lua_type(L, -1) == LUA_TSTRING) {
      replace_str = lua_tostring(L, -1);
    }
  }
  
  lua_pop(L, 1);
  
  if (!find_str || !replace_str) {
    lua_pushstring(L, src);
    return 1;
  }
  
  size_t find_len = strlen(find_str);
  size_t replace_len = strlen(replace_str);
  
  if (find_len == 0) {
    lua_pushstring(L, src);
    return 1;
  }
  
  size_t result_len = src_len;
  const char *pos = src;
  const char *end = src + src_len;
  
  if (use_regex) {
    while (pos < end) {
      size_t match_len = 0;
      const char *match = match_regex(pos, end - pos, find_str, &match_len);
      if (match) {
        result_len += replace_len - match_len;
        pos = match + match_len;
      } else {
        break;
      }
    }
    
    char *result = (char *)lua_newuserdatauv(L, result_len + 1, 0);
    
    pos = src;
    char *out = result;
    
    while (pos < end) {
      size_t match_len = 0;
      const char *match = match_regex(pos, end - pos, find_str, &match_len);
      if (match) {
        size_t prefix_len = match - pos;
        memcpy(out, pos, prefix_len);
        out += prefix_len;
        memcpy(out, replace_str, replace_len);
        out += replace_len;
        pos = match + match_len;
      } else {
        size_t suffix_len = end - pos;
        memcpy(out, pos, suffix_len);
        out += suffix_len;
        break;
      }
    }
    
    *out = '\0';
    lua_pushlstring(L, result, result_len);
  } else {
    int work_buf[256];
    
    const char *search_start = src;
    while ((search_start = fast_search(search_start, end - search_start, find_str, find_len, work_buf)) != NULL) {
      result_len += replace_len - find_len;
      search_start += find_len;
    }
    
    char *result = (char *)lua_newuserdatauv(L, result_len + 1, 0);
    
    const char *pos = src;
    char *out = result;
    const char *match_pos = src;
    
    while ((match_pos = fast_search(match_pos, end - match_pos, find_str, find_len, work_buf)) != NULL) {
      size_t prefix_len = match_pos - pos;
      memcpy(out, pos, prefix_len);
      out += prefix_len;
      memcpy(out, replace_str, replace_len);
      out += replace_len;
      pos = match_pos + find_len;
      match_pos = pos;
    }
    
    if (pos < end) {
      size_t suffix_len = end - pos;
      memcpy(out, pos, suffix_len);
      out += suffix_len;
    }
    
    *out = '\0';
    lua_pushlstring(L, result, result_len);
  }
  
  return 1;
}


/*
** fsleep(func) - 标记函数进入休眠模式
** 入参为目标函数，出参为成功返回true，失败返回false
*/
static int luaB_fsleep (lua_State *L) {
  if (lua_type(L, 1) != LUA_TFUNCTION) {
    lua_pushboolean(L, 0);
    return 1;
  }
  
  if (lua_iscfunction(L, 1)) {
    lua_pushboolean(L, 0);
    return 1;
  }
  
  lua_pushvalue(L, 1);
  LClosure *cl = clLvalue(s2v(L->top.p - 1));
  Proto *p = cl->p;
  L->top.p--;
  
  p->is_sleeping = 1;
  
  if (p->call_queue == NULL) {
    p->call_queue = luaF_newcallqueue(L);
  }
  
  lua_pushboolean(L, 1);
  return 1;
}


/*
** fwake(func) - 唤醒休眠函数，批量执行缓存的调用
** 入参为目标函数，出参为执行的调用次数
*/
static int luaB_fwake (lua_State *L) {
  if (lua_type(L, 1) != LUA_TFUNCTION) {
    lua_pushinteger(L, 0);
    return 1;
  }
  
  if (lua_iscfunction(L, 1)) {
    lua_pushinteger(L, 0);
    return 1;
  }
  
  lua_pushvalue(L, 1);
  LClosure *cl = clLvalue(s2v(L->top.p - 1));
  Proto *p = cl->p;
  
  if (!p->is_sleeping || p->call_queue == NULL || p->call_queue->size == 0) {
    L->top.p--;
    lua_pushinteger(L, 0);
    return 1;
  }
  
  CallQueue *q = p->call_queue;
  TValue args_buf[MAX_CALL_ARGS];
  int call_count = 0;
  int nargs;
  
  while (luaF_callqueuepop(L, q, &nargs, args_buf)) {
    call_count++;
    
    StkId func_addr = L->top.p;
    setobj(L, s2v(func_addr), s2v(L->top.p - 1));
    
    StkId args_addr = func_addr + 1;
    for (int i = 0; i < nargs; i++) {
      setobj(L, s2v(args_addr + i), &args_buf[i]);
    }
    
    L->top.p = args_addr + nargs;
    
    lua_call(L, nargs, 0);
  }
  
  L->top.p--;
  p->is_sleeping = 0;
  
  lua_pushinteger(L, call_count);
  return 1;
}

/*
** md5(str) - 计算字符串的 MD5 哈希值
** 参数：要计算哈希的字符串
** 返回：32位十六进制小写 MD5 哈希字符串
*/
static int luaB_md5 (lua_State *L) {
  size_t len;
  const char *str = luaL_checklstring(L, 1, &len);
  uint8_t digest[16];
  char hex_output[33];
  
  md5_compute((const uint8_t *)str, len, digest);
  
  for (int i = 0; i < 16; i++) {
    sprintf(hex_output + i * 2, "%02x", digest[i]);
  }
  hex_output[32] = '\0';
  
  lua_pushstring(L, hex_output);
  return 1;
}


/*
** ============================================================
** 条件测试函数 __test__
** 实现类似Shell的条件测试表达式语法
** ============================================================
*/

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

/*
** 辅助函数：检查操作符类型
** 参数：
**   op - 操作符字符串
** 返回值：
**   操作符类型代码
*/
static int get_test_op_type(const char *op) {
  if (!op || op[0] != '-') return 0;
  
  /* 文件测试操作符 */
  if (strcmp(op, "-e") == 0) return 1;   /* 存在 */
  if (strcmp(op, "-d") == 0) return 2;   /* 目录 */
  if (strcmp(op, "-f") == 0) return 3;   /* 普通文件 */
  if (strcmp(op, "-L") == 0) return 4;   /* 符号链接 */
  if (strcmp(op, "-b") == 0) return 5;   /* 块设备 */
  if (strcmp(op, "-c") == 0) return 6;   /* 字符设备 */
  if (strcmp(op, "-p") == 0) return 7;   /* 管道 */
  if (strcmp(op, "-S") == 0) return 8;   /* 套接字 */
  if (strcmp(op, "-r") == 0) return 9;   /* 可读 */
  if (strcmp(op, "-w") == 0) return 10;  /* 可写 */
  if (strcmp(op, "-x") == 0) return 11;  /* 可执行 */
  if (strcmp(op, "-u") == 0) return 12;  /* SUID */
  if (strcmp(op, "-g") == 0) return 13;  /* SGID */
  if (strcmp(op, "-k") == 0) return 14;  /* 粘滞位 */
  if (strcmp(op, "-s") == 0) return 15;  /* 非空文件 */
  if (strcmp(op, "-nt") == 0) return 16; /* 比...新 */
  if (strcmp(op, "-ot") == 0) return 17; /* 比...旧 */
  if (strcmp(op, "-size") == 0) return 18; /* 文件大小 */
  
  /* 数值比较操作符 */
  if (strcmp(op, "-eq") == 0) return 20; /* 相等 */
  if (strcmp(op, "-ne") == 0) return 21; /* 不相等 */
  if (strcmp(op, "-gt") == 0) return 22; /* 大于 */
  if (strcmp(op, "-lt") == 0) return 23; /* 小于 */
  if (strcmp(op, "-ge") == 0) return 24; /* 大于等于 */
  if (strcmp(op, "-le") == 0) return 25; /* 小于等于 */
  
  /* 字符串测试操作符 */
  if (strcmp(op, "-z") == 0) return 30;  /* 长度为0 */
  if (strcmp(op, "-n") == 0) return 31;  /* 长度大于0 */
  
  /* Lua专属操作符 */
  if (strcmp(op, "-type") == 0) return 40;   /* 类型测试 */
  if (strcmp(op, "-nil") == 0) return 41;    /* nil测试 */
  if (strcmp(op, "-bool") == 0) return 42;   /* 布尔测试 */
  if (strcmp(op, "-global") == 0) return 43; /* 全局变量测试 */
  if (strcmp(op, "-local") == 0) return 44;  /* 局部变量测试 */
  if (strcmp(op, "-haskey") == 0) return 45; /* 表键测试 */
  if (strcmp(op, "-len") == 0) return 46;    /* 表长度测试 */
  if (strcmp(op, "-func") == 0) return 47;   /* 函数测试 */
  if (strcmp(op, "-param") == 0) return 48;  /* 函数参数测试 */
  
  /* 逻辑操作符 */
  if (strcmp(op, "-a") == 0) return 50;  /* 逻辑与 */
  if (strcmp(op, "-o") == 0) return 51;  /* 逻辑或 */
  
  return 0;
}

/*
** 文件测试函数
** 参数：
**   path - 文件路径
**   op_type - 操作类型
** 返回值：
**   测试结果（布尔值）
*/
static int do_file_test(const char *path, int op_type) {
  struct stat st;
  int result = 0;
  
  switch (op_type) {
    case 1: /* -e 存在 */
      result = (stat(path, &st) == 0);
      break;
    case 2: /* -d 目录 */
      result = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
      break;
    case 3: /* -f 普通文件 */
      result = (stat(path, &st) == 0 && S_ISREG(st.st_mode));
      break;
    case 4: /* -L 符号链接 */
#ifdef _WIN32
      result = 0; /* Windows不支持符号链接检测 */
#else
      result = (lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
#endif
      break;
    case 5: /* -b 块设备 */
#ifdef _WIN32
      result = 0; /* Windows不支持块设备检测 */
#else
      result = (stat(path, &st) == 0 && S_ISBLK(st.st_mode));
#endif
      break;
    case 6: /* -c 字符设备 */
#ifdef _WIN32
      result = 0; /* Windows不支持字符设备检测 */
#else
      result = (stat(path, &st) == 0 && S_ISCHR(st.st_mode));
#endif
      break;
    case 7: /* -p 管道 */
#ifdef _WIN32
      result = 0; /* Windows不支持管道检测 */
#else
      result = (stat(path, &st) == 0 && S_ISFIFO(st.st_mode));
#endif
      break;
    case 8: /* -S 套接字 */
#ifdef _WIN32
      result = 0; /* Windows不支持套接字检测 */
#else
      result = (stat(path, &st) == 0 && S_ISSOCK(st.st_mode));
#endif
      break;
    case 9: /* -r 可读 */
      result = (access(path, R_OK) == 0);
      break;
    case 10: /* -w 可写 */
      result = (access(path, W_OK) == 0);
      break;
    case 11: /* -x 可执行 */
      result = (access(path, X_OK) == 0);
      break;
    case 12: /* -u SUID */
#ifdef _WIN32
      result = 0; /* Windows不支持SUID检测 */
#else
      result = (stat(path, &st) == 0 && (st.st_mode & S_ISUID));
#endif
      break;
    case 13: /* -g SGID */
#ifdef _WIN32
      result = 0; /* Windows不支持SGID检测 */
#else
      result = (stat(path, &st) == 0 && (st.st_mode & S_ISGID));
#endif
      break;
    case 14: /* -k 粘滞位 */
#ifdef _WIN32
      result = 0; /* Windows不支持粘滞位检测 */
#else
      result = (stat(path, &st) == 0 && (st.st_mode & S_ISVTX));
#endif
      break;
    case 15: /* -s 非空文件 */
      result = (stat(path, &st) == 0 && st.st_size > 0);
      break;
    default:
      result = 0;
  }
  
  return result;
}

/*
** __test__ - 条件测试表达式求值函数
** 支持文件测试、数值比较、字符串比较、Lua类型测试等
** 
** 语法形式：
**   [ -f "path" ]           -> __test__("-f", "path")
**   [ a -eq b ]             -> __test__(a, "-eq", b)
**   [ str1 = str2 ]         -> __test__(str1, "=", str2)
**   [ -type var "table" ]   -> __test__("-type", var, "table")
**   [ ! condition ]         -> __test__("!", condition)
**   [ cond1 -a cond2 ]      -> __test__(cond1, "-a", cond2)
**
** 参数：
**   可变参数，根据测试类型不同
** 返回值：
**   布尔值，表示测试结果
*/
static int async_start(lua_State *L) {
    int n = lua_gettop(L);
    lua_State *co = lua_newthread(L);
    lua_insert(L, 1); /* Move thread to bottom (stack: thread, arg1, arg2...) */

    /* Get wrapper from registry */
    lua_getfield(L, LUA_REGISTRYINDEX, "_ASYNC_LAZY_WRAPPER");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        /* Create wrapper if not exists (lazy init) */
        if (luaL_dostring(L, "return function(f, ...) coroutine.yield(); return f(...) end") != LUA_OK) {
            return lua_error(L);
        }
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_ASYNC_LAZY_WRAPPER");
    }
    lua_xmove(L, co, 1);

    /* Push function */
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_xmove(L, co, 1);

    /* Move arguments */
    lua_xmove(L, co, n);

    int nres;
    int status = lua_resume(co, L, n + 1, &nres);

    if (status != LUA_YIELD) {
        if (status != LUA_OK) {
            lua_xmove(co, L, 1);
            return lua_error(L);
        }
    }

    if (nres > 0) {
        lua_pop(co, nres);
    }

    return 1;
}

static int luaB_async_wrap(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, async_start, 1);
    return 1;
}

static int luaB_test (lua_State *L) {
  int nargs = lua_gettop(L);
  
  if (nargs == 0) {
    lua_pushboolean(L, 0);
    return 1;
  }
  
  /* 单参数情况：检查值是否为真 */
  if (nargs == 1) {
    lua_pushboolean(L, lua_toboolean(L, 1));
    return 1;
  }
  
  /* 获取第一个参数 */
  const char *first_str = NULL;
  if (lua_type(L, 1) == LUA_TSTRING) {
    first_str = lua_tostring(L, 1);
  }
  
  /* 处理逻辑非操作符 ! */
  if (first_str && strcmp(first_str, "!") == 0) {
    /* 递归调用处理剩余参数 */
    lua_remove(L, 1);  /* 移除 "!" */
    luaB_test(L);
    int result = !lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, result);
    return 1;
  }
  
  /* 双参数情况 */
  if (nargs == 2) {
    if (first_str) {
      int op_type = get_test_op_type(first_str);
      
      /* 文件测试: [ -f "path" ] */
      if (op_type >= 1 && op_type <= 15) {
        const char *path = luaL_checkstring(L, 2);
        lua_pushboolean(L, do_file_test(path, op_type));
        return 1;
      }
      
      /* 字符串空值测试 */
      if (op_type == 30) { /* -z 长度为0 */
        if (lua_type(L, 2) == LUA_TSTRING) {
          size_t len;
          lua_tolstring(L, 2, &len);
          lua_pushboolean(L, len == 0);
        } else if (lua_isnil(L, 2)) {
          lua_pushboolean(L, 1);
        } else {
          lua_pushboolean(L, 0);
        }
        return 1;
      }
      if (op_type == 31) { /* -n 长度大于0 */
        if (lua_type(L, 2) == LUA_TSTRING) {
          size_t len;
          lua_tolstring(L, 2, &len);
          lua_pushboolean(L, len > 0);
        } else if (lua_isnil(L, 2)) {
          lua_pushboolean(L, 0);
        } else {
          lua_pushboolean(L, 1);
        }
        return 1;
      }
      
      /* Lua专属：nil测试 */
      if (op_type == 41) { /* -nil */
        lua_pushboolean(L, lua_isnil(L, 2));
        return 1;
      }
      
      /* Lua专属：布尔测试 */
      if (op_type == 42) { /* -bool */
        lua_pushboolean(L, lua_isboolean(L, 2));
        return 1;
      }
      
      /* Lua专属：函数测试 */
      if (op_type == 47) { /* -func */
        lua_pushboolean(L, lua_isfunction(L, 2));
        return 1;
      }
      
      /* Lua专属：全局变量测试 */
      if (op_type == 43) { /* -global */
        const char *name = luaL_checkstring(L, 2);
        lua_getglobal(L, name);
        int exists = !lua_isnil(L, -1);
        lua_pop(L, 1);
        lua_pushboolean(L, exists);
        return 1;
      }
    }
  }
  
  /* 三参数情况 */
  if (nargs == 3) {
    const char *op_str = NULL;
    if (lua_type(L, 2) == LUA_TSTRING) {
      op_str = lua_tostring(L, 2);
    }
    
    if (op_str) {
      int op_type = get_test_op_type(op_str);
      
      /* 文件时间比较 */
      if (op_type == 16 || op_type == 17) { /* -nt, -ot */
        const char *path1 = luaL_checkstring(L, 1);
        const char *path2 = luaL_checkstring(L, 3);
        struct stat st1, st2;
        if (stat(path1, &st1) != 0 || stat(path2, &st2) != 0) {
          lua_pushboolean(L, 0);
        } else if (op_type == 16) { /* -nt 比...新 */
          lua_pushboolean(L, st1.st_mtime > st2.st_mtime);
        } else { /* -ot 比...旧 */
          lua_pushboolean(L, st1.st_mtime < st2.st_mtime);
        }
        return 1;
      }
      
      /* 文件大小比较 */
      if (op_type == 18) { /* -size */
        const char *path = luaL_checkstring(L, 1);
        lua_Integer size = luaL_checkinteger(L, 3);
        struct stat st;
        if (stat(path, &st) == 0) {
          lua_pushboolean(L, st.st_size >= size);
        } else {
          lua_pushboolean(L, 0);
        }
        return 1;
      }
      
      /* 数值比较 */
      if (op_type >= 20 && op_type <= 25) {
        lua_Number a = luaL_checknumber(L, 1);
        lua_Number b = luaL_checknumber(L, 3);
        int result = 0;
        switch (op_type) {
          case 20: result = (a == b); break; /* -eq */
          case 21: result = (a != b); break; /* -ne */
          case 22: result = (a > b); break;  /* -gt */
          case 23: result = (a < b); break;  /* -lt */
          case 24: result = (a >= b); break; /* -ge */
          case 25: result = (a <= b); break; /* -le */
        }
        lua_pushboolean(L, result);
        return 1;
      }
      
      /* 字符串比较 */
      if (strcmp(op_str, "=") == 0 || strcmp(op_str, "==") == 0) {
        const char *s1 = lua_tostring(L, 1);
        const char *s2 = lua_tostring(L, 3);
        if (s1 && s2) {
          lua_pushboolean(L, strcmp(s1, s2) == 0);
        } else {
          lua_pushboolean(L, lua_rawequal(L, 1, 3));
        }
        return 1;
      }
      if (strcmp(op_str, "!=") == 0) {
        const char *s1 = lua_tostring(L, 1);
        const char *s2 = lua_tostring(L, 3);
        if (s1 && s2) {
          lua_pushboolean(L, strcmp(s1, s2) != 0);
        } else {
          lua_pushboolean(L, !lua_rawequal(L, 1, 3));
        }
        return 1;
      }
      
      /* 模式匹配 */
      if (strcmp(op_str, "=~") == 0) {
        const char *s = luaL_checkstring(L, 1);
        const char *pattern = luaL_checkstring(L, 3);
        /* 使用Lua的string.match */
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "match");
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 1);
        lua_pushboolean(L, !lua_isnil(L, -1));
        return 1;
      }
      if (strcmp(op_str, "!~") == 0) {
        const char *s = luaL_checkstring(L, 1);
        const char *pattern = luaL_checkstring(L, 3);
        lua_getglobal(L, "string");
        lua_getfield(L, -1, "match");
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 1);
        lua_pushboolean(L, lua_isnil(L, -1));
        return 1;
      }
      
      /* 逻辑运算 */
      if (op_type == 50) { /* -a 逻辑与 */
        int a = lua_toboolean(L, 1);
        int b = lua_toboolean(L, 3);
        lua_pushboolean(L, a && b);
        return 1;
      }
      if (op_type == 51) { /* -o 逻辑或 */
        int a = lua_toboolean(L, 1);
        int b = lua_toboolean(L, 3);
        lua_pushboolean(L, a || b);
        return 1;
      }
      
      /* Lua专属：类型测试 [ -type var "typename" ] */
      if (op_type == 40) { /* -type */
        const char *expected_type = luaL_checkstring(L, 3);
        const char *actual_type = luaL_typename(L, 2);
        lua_pushboolean(L, strcmp(actual_type, expected_type) == 0);
        return 1;
      }
      
      /* Lua专属：表键测试 [ -haskey table key ] */
      if (op_type == 45) { /* -haskey */
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushvalue(L, 3);
        lua_gettable(L, 2);
        lua_pushboolean(L, !lua_isnil(L, -1));
        return 1;
      }
      
      /* Lua专属：表长度测试 [ -len table n ] */
      if (op_type == 46) { /* -len */
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_Integer expected_len = luaL_checkinteger(L, 3);
        lua_Integer actual_len = luaL_len(L, 2);
        lua_pushboolean(L, actual_len == expected_len);
        return 1;
      }
    }
    
    /* 第一个参数是操作符的情况 */
    if (first_str) {
      int op_type = get_test_op_type(first_str);
      
      /* Lua专属：类型测试（第一个参数是操作符） */
      if (op_type == 40) { /* -type var typename */
        const char *expected_type = luaL_checkstring(L, 3);
        const char *actual_type = luaL_typename(L, 2);
        lua_pushboolean(L, strcmp(actual_type, expected_type) == 0);
        return 1;
      }
      
      /* Lua专属：表键测试 */
      if (op_type == 45) { /* -haskey table key */
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushvalue(L, 3);
        lua_gettable(L, 2);
        lua_pushboolean(L, !lua_isnil(L, -1));
        return 1;
      }
      
      /* Lua专属：表长度测试 */
      if (op_type == 46) { /* -len table n */
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_Integer expected_len = luaL_checkinteger(L, 3);
        lua_Integer actual_len = luaL_len(L, 2);
        lua_pushboolean(L, actual_len == expected_len);
        return 1;
      }
    }
  }
  
  /* 四参数情况 */
  if (nargs == 4) {
    if (first_str) {
      int op_type = get_test_op_type(first_str);
      
      /* Lua专属：函数参数测试 [ -param func n ] */
      if (op_type == 48) { /* -param */
        luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_Integer expected_params = luaL_checkinteger(L, 3);
        /* 获取函数信息 */
        lua_Debug ar;
        lua_pushvalue(L, 2);
        lua_getinfo(L, ">u", &ar);
        lua_pushboolean(L, ar.nparams == expected_params);
        return 1;
      }
    }
  }
  
  /* 处理复合逻辑表达式（5个及以上参数）*/
  /* 格式如: -f "a.txt" -a -r "a.txt" (5个参数)
   * 或: cond1 -a cond2 -o cond3 (更多参数)
   */
  if (nargs >= 5) {
    /* 查找 -a 或 -o 操作符的位置 */
    int logic_op_pos = -1;
    int logic_op_type = 0;
    
    for (int i = 1; i <= nargs; i++) {
      if (lua_type(L, i) == LUA_TSTRING) {
        const char *s = lua_tostring(L, i);
        int op = get_test_op_type(s);
        if (op == 50 || op == 51) {  /* -a 或 -o */
          logic_op_pos = i;
          logic_op_type = op;
          break;
        }
      }
    }
    
    if (logic_op_pos > 1 && logic_op_pos < nargs) {
      /* 找到逻辑操作符，分割并递归求值 */
      /* 左侧参数: 1 到 logic_op_pos-1 */
      /* 右侧参数: logic_op_pos+1 到 nargs */
      
      /* 求值左侧表达式 */
      int left_count = logic_op_pos - 1;
      lua_pushcfunction(L, luaB_test);
      for (int i = 1; i <= left_count; i++) {
        lua_pushvalue(L, i);
      }
      lua_call(L, left_count, 1);
      int left_result = lua_toboolean(L, -1);
      lua_pop(L, 1);
      
      /* 短路求值 */
      if (logic_op_type == 50) {  /* -a (AND) */
        if (!left_result) {
          lua_pushboolean(L, 0);
          return 1;
        }
      } else {  /* -o (OR) */
        if (left_result) {
          lua_pushboolean(L, 1);
          return 1;
        }
      }
      
      /* 求值右侧表达式 */
      int right_start = logic_op_pos + 1;
      int right_count = nargs - logic_op_pos;
      lua_pushcfunction(L, luaB_test);
      for (int i = right_start; i <= nargs; i++) {
        lua_pushvalue(L, i);
      }
      lua_call(L, right_count, 1);
      int right_result = lua_toboolean(L, -1);
      lua_pop(L, 1);
      
      if (logic_op_type == 50) {  /* -a (AND) */
        lua_pushboolean(L, left_result && right_result);
      } else {  /* -o (OR) */
        lua_pushboolean(L, left_result || right_result);
      }
      return 1;
    }
  }
  
  /* 默认情况：检查所有参数是否为真 */
  int result = 1;
  for (int i = 1; i <= nargs; i++) {
    if (!lua_toboolean(L, i)) {
      result = 0;
      break;
    }
  }
  lua_pushboolean(L, result);
  return 1;
}

static int luaB_typeof(lua_State *L) {
  luaL_checkany(L, 1);
  if (lua_type(L, 1) == LUA_TSTRUCT) {
    const TValue *o = s2v(L->top.p - 1);
    Struct *s = structvalue(o);
    lua_lock(L);
    sethvalue(L, s2v(L->top.p), s->def);
    api_incr_top(L);
    lua_unlock(L);
    return 1;
  }
  lua_pushstring(L, luaL_typename(L, 1));
  return 1;
}

static int check_subtype(lua_State *L, int val_idx, int type_idx) {
    if (lua_type(L, type_idx) == LUA_TSTRING) {
        const char *tname = lua_tostring(L, type_idx);
        if (strcmp(tname, "any") == 0) return 1;
        if (strcmp(tname, "int") == 0 || strcmp(tname, "integer") == 0) return lua_isinteger(L, val_idx);
        if (strcmp(tname, "number") == 0) return lua_type(L, val_idx) == LUA_TNUMBER;
        if (strcmp(tname, "float") == 0) return lua_type(L, val_idx) == LUA_TNUMBER;
        if (strcmp(tname, "string") == 0) return lua_type(L, val_idx) == LUA_TSTRING;
        if (strcmp(tname, "boolean") == 0) return lua_type(L, val_idx) == LUA_TBOOLEAN;
        if (strcmp(tname, "table") == 0) return lua_type(L, val_idx) == LUA_TTABLE;
        if (strcmp(tname, "function") == 0) return lua_type(L, val_idx) == LUA_TFUNCTION;
        if (strcmp(tname, "thread") == 0) return lua_type(L, val_idx) == LUA_TTHREAD;
        if (strcmp(tname, "userdata") == 0) return lua_type(L, val_idx) == LUA_TUSERDATA;
        if (strcmp(tname, "nil") == 0 || strcmp(tname, "void") == 0) return lua_type(L, val_idx) == LUA_TNIL;
        return 0;
    }
    else if (lua_type(L, type_idx) == LUA_TTABLE) {
        lua_getglobal(L, "string");
        if (lua_rawequal(L, -1, type_idx)) {
            lua_pop(L, 1);
            return lua_type(L, val_idx) == LUA_TSTRING;
        }
        lua_pop(L, 1);

        lua_getglobal(L, "table");
        if (lua_rawequal(L, -1, type_idx)) {
            lua_pop(L, 1);
            return lua_type(L, val_idx) == LUA_TTABLE;
        }
        lua_pop(L, 1);

        return luaC_instanceof(L, val_idx, type_idx);
    }
    return 0;
}

static int luaB_issubtype(lua_State *L) {
  luaL_checkany(L, 1);
  luaL_checkany(L, 2);
  lua_pushboolean(L, check_subtype(L, 1, 2));
  return 1;
}

static int luaB_check_type(lua_State *L) {
    luaL_checkany(L, 1);
    luaL_checkany(L, 2);

    if (!check_subtype(L, 1, 2)) {
        const char *name = luaL_optstring(L, 3, "?");
        const char *expected = "unknown";
        if (lua_type(L, 2) == LUA_TSTRING) expected = lua_tostring(L, 2);
        else if (lua_type(L, 2) == LUA_TTABLE) {
             lua_getfield(L, 2, "__name");
             if (lua_isstring(L, -1)) expected = lua_tostring(L, -1);
             else {
                 lua_getglobal(L, "string");
                 if (lua_rawequal(L, -1, 2)) expected = "string";
                 lua_pop(L, 1);

                 if (strcmp(expected, "string") != 0) {
                     lua_getglobal(L, "table");
                     if (lua_rawequal(L, -1, 2)) expected = "table";
                     lua_pop(L, 1);
                 }
                 if (strcmp(expected, "unknown") == 0) expected = "table";
             }
             lua_pop(L, 1);
        }

        return luaL_error(L, "Type mismatch for argument '%s': expected %s, got %s",
                          name, expected, luaL_typename(L, 1));
    }
    return 0;
}

static int luaB_isgeneric(lua_State *L) {
  if (lua_istable(L, 1)) {
     lua_pushstring(L, "__is_generic");
     lua_rawget(L, 1);
     int res = lua_toboolean(L, -1);
     lua_pop(L, 1);
     lua_pushboolean(L, res);
     return 1;
  }
  lua_pushboolean(L, 0);
  return 1;
}

static int generic_call (lua_State *L) {
    /* Upvalues: 1:factory, 2:params, 3:mapping */
    /* Called as __call(self, args...) */
    int nargs = lua_gettop(L) - 1;
    int base = 2;
    int is_specialization = 0;

    if (nargs >= 1) {
        int t = lua_type(L, base);
        if (t == LUA_TSTRING) {
            const char *s = lua_tostring(L, base);
            if (strcmp(s, "number")==0 || strcmp(s, "string")==0 ||
                strcmp(s, "boolean")==0 || strcmp(s, "table")==0 ||
                strcmp(s, "function")==0 || strcmp(s, "thread")==0 ||
                strcmp(s, "userdata")==0 || strcmp(s, "nil_type")==0) {
                is_specialization = 1;
            }
        } else if (t == LUA_TTABLE) {
            /* Check for libraries used as types */
            lua_getglobal(L, "string");
            if (lua_rawequal(L, -1, base)) is_specialization = 1;
            lua_pop(L, 1);

            if (!is_specialization) {
                lua_getglobal(L, "table");
                if (lua_rawequal(L, -1, base)) is_specialization = 1;
                lua_pop(L, 1);
            }

            if (!is_specialization) {
                lua_getfield(L, base, "__name");
                if (!lua_isnil(L, -1)) is_specialization = 1;
                lua_pop(L, 1);
            }
        }
    }

    if (is_specialization) {
        lua_pushvalue(L, lua_upvalueindex(1)); /* factory */
        for (int i = 0; i < nargs; i++) {
            lua_pushvalue(L, base + i);
        }
        lua_call(L, nargs, LUA_MULTRET);
        return lua_gettop(L) - (nargs + 1);
    }

    /* Inference */
    lua_newtable(L); /* inferred map */
    int inferred_idx = lua_gettop(L);

    int nmapping = luaL_len(L, lua_upvalueindex(3));
    for (int i = 0; i < nargs && i < nmapping; i++) {
        lua_rawgeti(L, lua_upvalueindex(3), i + 1);
        const char *param_type_name = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (param_type_name) {
            int nparams = luaL_len(L, lua_upvalueindex(2));
            int is_generic = 0;
            for (int j = 1; j <= nparams; j++) {
                lua_rawgeti(L, lua_upvalueindex(2), j);
                const char *gp = lua_tostring(L, -1);
                lua_pop(L, 1);
                if (gp && strcmp(gp, param_type_name) == 0) {
                    is_generic = 1;
                    break;
                }
            }

            if (is_generic) {
                lua_pushvalue(L, base + i);
                if (lua_type(L, -1) == LUA_TSTRUCT) {
                    const TValue *o = s2v(L->top.p - 1);
                    Struct *s = structvalue(o);
                    lua_lock(L);
                    sethvalue(L, s2v(L->top.p), s->def);
                    L->top.p++;
                    lua_unlock(L);
                    lua_remove(L, -2);
                } else {
                    lua_pushstring(L, luaL_typename(L, -1));
                    lua_remove(L, -2);
                }

                lua_pushstring(L, param_type_name);
                lua_rawget(L, inferred_idx);
                if (!lua_isnil(L, -1)) {
                    if (!lua_compare(L, -1, -2, LUA_OPEQ)) {
                        return luaL_error(L, "type inference failed: inconsistent types for '%s'", param_type_name);
                    }
                    lua_pop(L, 2);
                } else {
                    lua_pop(L, 1);
                    lua_pushstring(L, param_type_name);
                    lua_pushvalue(L, -2);
                    lua_rawset(L, inferred_idx);
                    lua_pop(L, 1);
                }
            }
        }
    }

    int nparams = luaL_len(L, lua_upvalueindex(2));
    lua_pushvalue(L, lua_upvalueindex(1));

    for (int j = 1; j <= nparams; j++) {
        lua_rawgeti(L, lua_upvalueindex(2), j);
        const char *gp = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_pushstring(L, gp);
        lua_rawget(L, inferred_idx);
        if (lua_isnil(L, -1)) {
            return luaL_error(L, "could not infer type for '%s'", gp);
        }
    }

    lua_call(L, nparams, 1); /* impl */

    int impl_idx = lua_gettop(L);
    lua_pushvalue(L, impl_idx);
    for (int i = 0; i < nargs; i++) {
       lua_pushvalue(L, base + i);
    }
    lua_call(L, nargs, LUA_MULTRET);
    return lua_gettop(L) - impl_idx;
}

static int luaB_generic_wrap(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TTABLE);

    lua_newtable(L); /* wrapper table */
    lua_newtable(L); /* metatable */

    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_pushcclosure(L, generic_call, 3);
    lua_setfield(L, -2, "__call");

    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "__is_generic");

    lua_setmetatable(L, -2);
    return 1;
}

static const luaL_Reg base_funcs[] = {
  {"__async_wrap", luaB_async_wrap},
  {"__generic_wrap", luaB_generic_wrap},
  {"__check_type", luaB_check_type},
  {"typeof", luaB_typeof},
  {"issubtype", luaB_issubtype},
  {"isgeneric", luaB_isgeneric},
  {"assert", luaB_assert},
  {"collectgarbage", luaB_collectgarbage},
  {"defer", luaB_defer},
  {"dofile", luaB_dofile},
  {"dump", luaB_dump},
  {"error", luaB_error},
    {"grand", luaB_grand},
  {"fsleep", luaB_fsleep},
#if defined(LUA_COMPAT_MODULE)
        {"findtable", findtable},
#endif
  {"getenv", luaB_getenv_original},
  {"getfenv", luaB_getfenv},
  {"getmetatable", luaB_getmetatable},
  {"ipairs", luaB_ipairs},
  {"loadfile", luaB_loadfile},
  {"loadsfile", luaB_loadsfile},
  {"load", luaB_load},
  {"loadstring", luaB_load},
  {"next", luaB_next},
  {"pairs", luaB_pairs},
  {"pcall", luaB_pcall},
  {"print", luaB_print},
  {"warn", luaB_warn},
  {"rawequal", luaB_rawequal},
  {"rawlen", luaB_rawlen},
  {"rawget", luaB_rawget},
  {"rawset", luaB_rawset},
  {"select", luaB_select},
  {"setfenv", luaB_setfenv},
  {"setmetatable", luaB_setmetatable},
  {"tonumber", luaB_tonumber},
  {"tointeger", luaB_tointeger},
  {"tostring", luaB_tostring},
  {"toasc2i", luaB_toasc2i},  /* 新增函数：将字符串转换为ASCII十六进制转义格式 */
  {"match", luaB_match},
  {"fwake", luaB_fwake},
  {"wymd5", luaB_md5},
  {"type", luaB_type},
  {"isstruct", luaB_isstruct},
  {"isinstance", luaB_isinstance},
  {"__test__", luaB_test},
  {"xpcall", luaB_xpcall},
  /* placeholders */
  {LUA_GNAME, NULL},
  {"_VERSION", NULL},
  {NULL, NULL}
};


/*
** with 语句的 __index 元方法
** 优先从目标表查找字段，找不到则从原环境查找
** upvalue 1: 目标表
** upvalue 2: 原环境
*/
static int with_index (lua_State *L) {
  /* 先从目标表查找 */
  lua_pushvalue(L, lua_upvalueindex(1));  /* 目标表 */
  lua_pushvalue(L, 2);  /* key */
  lua_gettable(L, -2);  /* target[key]，使用 gettable 以支持元表 */
  
  if (!lua_isnil(L, -1)) {
    return 1;  /* 找到了，返回 */
  }
  lua_pop(L, 2);  /* 弹出 nil 和目标表 */
  
  /* 从原环境查找，使用 gettable 以支持嵌套 with */
  lua_pushvalue(L, lua_upvalueindex(2));  /* 原环境 */
  lua_pushvalue(L, 2);  /* key */
  lua_gettable(L, -2);  /* env[key]，触发元表 __index */
  
  return 1;
}


/*
** with 语句的 __newindex 元方法
** 直接写入目标表
** upvalue 1: 目标表
*/
static int with_newindex (lua_State *L) {
  lua_pushvalue(L, lua_upvalueindex(1));  /* 目标表 */
  lua_pushvalue(L, 2);  /* key */
  lua_pushvalue(L, 3);  /* value */
  lua_rawset(L, -3);  /* target[key] = value */
  return 0;
}


/*
** 创建 with 语句的新环境
** 参数 1: 目标表
** 参数 2: 原环境
** 返回: 带元表的新环境
*/
static int with_create_env (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);  /* 目标表 */
  luaL_checktype(L, 2, LUA_TTABLE);  /* 原环境 */
  
  /* 创建新环境表 */
  lua_newtable(L);  /* 新环境 */
  
  /* 创建元表 */
  lua_createtable(L, 0, 2);  /* 元表 */
  
  /* 设置 __index */
  lua_pushvalue(L, 1);  /* 目标表 */
  lua_pushvalue(L, 2);  /* 原环境 */
  lua_pushcclosure(L, with_index, 2);  /* 闭包 with_index */
  lua_setfield(L, -2, "__index");
  
  /* 设置 __newindex */
  lua_pushvalue(L, 1);  /* 目标表 */
  lua_pushcclosure(L, with_newindex, 1);  /* 闭包 with_newindex */
  lua_setfield(L, -2, "__newindex");
  
  /* 将元表设置到新环境 */
  lua_setmetatable(L, -2);
  
  return 1;
}


static int protect_global (lua_State *L) {
  const char *name = lua_tostring(L, 2);
  
  // 检查 name 是否为 NULL
  if (name == NULL) {
    // 允许非字符串键
    lua_rawset(L, 1);
    return 0;
  }
  
  // 检查是否为受保护的函数名
  if (strcmp(name, "getenv") == 0) {
    return luaL_error(L, "无法修改受保护的函数 '%s'", name);
  }
  
  // 允许修改其他全局变量
  lua_rawset(L, 1);
  return 0;
}

LUAMOD_API int luaopen_base (lua_State *L) {
  /* open lib into global table */
  lua_pushglobaltable(L);
  luaL_setfuncs(L, base_funcs, 0);
  /* set global _G */
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, LUA_GNAME);
  /* set global _VERSION */
  lua_pushliteral(L, LUA_VERSION);
  lua_setfield(L, -2, "_VERSION");
  
  /* 初始化全局 _CMDS 表，用于存储已注册的命令 */
  lua_newtable(L);
  lua_setfield(L, -2, "_CMDS");
  
  /* 注册 with 语句辅助函数 */
  lua_pushcfunction(L, with_create_env);
  lua_setfield(L, -2, "__with_create_env__");
  
  /* 设置全局表元表，保护核心函数不被修改 */
  lua_createtable(L, 0, 1);  /* 创建元表 */
  lua_pushcfunction(L, protect_global);  /* __newindex元方法 */
  lua_setfield(L, -2, "__newindex");
  
  /* 允许用户修改全局表的元表，但保留__newindex保护 */
  lua_setmetatable(L, -2);  /* 设置全局表元表 */

  /* Define global type constants */
  lua_pushliteral(L, "number"); lua_setfield(L, -2, "number");
  /* string is standard library */
  lua_pushliteral(L, "boolean"); lua_setfield(L, -2, "boolean");
  /* table is standard library */
  /* function is keyword */
  lua_pushliteral(L, "thread"); lua_setfield(L, -2, "thread");
  lua_pushliteral(L, "userdata"); lua_setfield(L, -2, "userdata");
  lua_pushliteral(L, "nil"); lua_setfield(L, -2, "nil_type"); /* avoid clashing with nil value */
  
  return 1;
}

