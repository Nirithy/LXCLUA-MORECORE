/*
** boolean library for Lua
** See Copyright Notice in lua.h
*/

#define lboolib_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


static int bool_to_string (lua_State *L) {
  int b = lua_toboolean(L, 1);
  lua_pushstring(L, b ? "true" : "false");
  return 1;
}


static int bool_to_number (lua_State *L) {
  int b = lua_toboolean(L, 1);
  lua_pushnumber(L, (lua_Number)(b ? 1 : 0));
  return 1;
}


static int bool_negate (lua_State *L) {
  int b = lua_toboolean(L, 1);
  lua_pushboolean(L, !b);
  return 1;
}


static int bool_and (lua_State *L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (!lua_toboolean(L, i)) {
      lua_pushboolean(L, 0);
      return 1;
    }
  }
  lua_pushboolean(L, 1);
  return 1;
}


static int bool_or (lua_State *L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (lua_toboolean(L, i)) {
      lua_pushboolean(L, 1);
      return 1;
    }
  }
  lua_pushboolean(L, 0);
  return 1;
}


static int bool_xor (lua_State *L) {
  int a = lua_toboolean(L, 1);
  int b = lua_toboolean(L, 2);
  lua_pushboolean(L, (a && !b) || (!a && b));
  return 1;
}


static int bool_eq (lua_State *L) {
  int a = lua_toboolean(L, 1);
  int b = lua_toboolean(L, 2);
  lua_pushboolean(L, a == b);
  return 1;
}


static int bool_is_boolean (lua_State *L) {
  lua_pushboolean(L, lua_type(L, 1) == LUA_TBOOLEAN);
  return 1;
}


/* Helper function to generate a random character */
static char random_char() {
  /* Generate a random printable ASCII character (33-126) */
  return (char)(33 + (rand() % 94));
}

/* Helper function to generate a random string */
static void random_string(char *buf, size_t len) {
  if (len == 0) return;
  
  /* Generate random length between 1 and len-1 */
  size_t str_len = 1 + (rand() % (len - 1));
  
  for (size_t i = 0; i < str_len; i++) {
    buf[i] = random_char();
  }
  
  buf[str_len] = '\0';
}

static int bool_toexpr (lua_State *L) {
  int b = lua_toboolean(L, 1);
  char expr_buf[512];
  
  if (b) {
    /* Generate complex nested expression that evaluates to true */
    int expr_type = rand() % 6;
    char random_str[32];
    random_string(random_str, sizeof(random_str));
    
    switch (expr_type) {
      case 0: /* Simple true expression with negation */
        sprintf(expr_buf, "not false");
        break;
      case 1: /* Random string with multiple logic gates */
        sprintf(expr_buf, "((\"%s\" and 123) or false) and not (false or nil)", 
                random_str);
        break;
      case 2: /* Nested logic gates with comparison */
        sprintf(expr_buf, "((%d > %d) and (\"%c\" ~= nil)) or (not false)", 
                1 + rand() % 100, rand() % 100, random_char());
        break;
      case 3: /* Complex nested structure */
        sprintf(expr_buf, "not (not ((true and true) and (\"%s\" or true))) and (true and not false)", 
                random_str);
        break;
      case 4: /* Multiple and/or chains */
        sprintf(expr_buf, "(true and true and true) or (not false and true)");
        break;
      case 5: /* Random combination with comparison */
        sprintf(expr_buf, "((10 > 5) and (\"test\" ~= nil)) or (not false)");
        break;
      default:
        strcpy(expr_buf, "true");
        break;
    }
  } else {
    /* Generate complex nested expression that evaluates to false */
    int expr_type = rand() % 6;
    char random_str[32];
    random_string(random_str, sizeof(random_str));
    
    switch (expr_type) {
      case 0: /* Simple false expression with negation */
        sprintf(expr_buf, "not true");
        break;
      case 1: /* Complex false expression with string */
        sprintf(expr_buf, "((false or false) and nil) or (nil and true)");
        break;
      case 2: /* Comparison with false result */
        sprintf(expr_buf, "((%d < %d) and (nil == nil)) or (not true)", 
                rand() % 100, 1 + rand() % 100);
        break;
      case 3: /* Double negation of false */
        sprintf(expr_buf, "not (not (not true)) and (false and false)");
        break;
      case 4: /* Multiple false chain */
        sprintf(expr_buf, "(false and false and false) or (nil and not false)");
        break;
      case 5: /* Complex nested false with comparison */
        sprintf(expr_buf, "((5 < 3) and not (true or true)) and (\"test\" == nil)");
        break;
      default:
        strcpy(expr_buf, "false");
        break;
    }
  }
  
  lua_pushstring(L, expr_buf);
  return 1;
}


static const luaL_Reg bool_funcs[] = {
  {"tostring", bool_to_string},
  {"tonumber", bool_to_number},
  {"not", bool_negate},
  {"and", bool_and},
  {"or", bool_or},
  {"xor", bool_xor},
  {"eq", bool_eq},
  {"is", bool_is_boolean},
  {"toexpr", bool_toexpr},
  {NULL, NULL}
};


LUAMOD_API int luaopen_bool (lua_State *L) {
  luaL_newlib(L, bool_funcs);
  return 1;
}
