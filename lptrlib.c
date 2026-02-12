/*
** $Id: lptrlib.c $
** Pointer manipulation library for Lua
** See Copyright Notice in lua.h
*/

#define lptrlib_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


/*
** Implementation of pointer library functions
*/

static int l_ptr_addr (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  lua_pushinteger(L, (lua_Integer)p);
  return 1;
}


static int l_ptr_add (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  ptrdiff_t offset = luaL_checkinteger(L, 2);
  p = (char *)p + offset;
  lua_pushpointer(L, (void *)p);
  return 1;
}

static int l_ptr_inc (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  ptrdiff_t step = luaL_optinteger(L, 2, 1);
  p = (char *)p + step;
  lua_pushpointer(L, (void *)p);
  return 1;
}

static int l_ptr_dec (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  ptrdiff_t step = luaL_optinteger(L, 2, 1);
  p = (char *)p - step;
  lua_pushpointer(L, (void *)p);
  return 1;
}


static int l_ptr_sub (lua_State *L) {
  const void *p1 = lua_topointer(L, 1);
  if (lua_ispointer(L, 2)) {
    const void *p2 = lua_topointer(L, 2);
    ptrdiff_t diff = (char *)p1 - (char *)p2;
    lua_pushinteger(L, diff);
  } else {
    ptrdiff_t offset = luaL_checkinteger(L, 2);
    p1 = (char *)p1 - offset;
    lua_pushpointer(L, (void *)p1);
  }
  return 1;
}

static void ptr_read_value (lua_State *L, const void *p, const char *type) {
  if (strcmp(type, "int") == 0) {
    lua_pushinteger(L, *(const int *)p);
  } else if (strcmp(type, "float") == 0) {
    lua_pushnumber(L, *(const float *)p);
  } else if (strcmp(type, "double") == 0) {
    lua_pushnumber(L, *(const double *)p);
  } else if (strcmp(type, "char") == 0) {
    lua_pushinteger(L, *(const char *)p);
  } else if (strcmp(type, "unsigned char") == 0 || strcmp(type, "byte") == 0) {
    lua_pushinteger(L, *(const unsigned char *)p);
  } else if (strcmp(type, "unsigned int") == 0) {
    lua_pushinteger(L, *(const unsigned int *)p);
  } else if (strcmp(type, "short") == 0) {
    lua_pushinteger(L, *(const short *)p);
  } else if (strcmp(type, "unsigned short") == 0) {
    lua_pushinteger(L, *(const unsigned short *)p);
  } else if (strcmp(type, "long") == 0) {
    lua_pushinteger(L, *(const long *)p);
  } else if (strcmp(type, "unsigned long") == 0) {
    lua_pushinteger(L, *(const unsigned long *)p);
  } else if (strcmp(type, "size_t") == 0) {
    lua_pushinteger(L, (lua_Integer)*(const size_t *)p);
  } else if (strcmp(type, "lua_Integer") == 0) {
    lua_pushinteger(L, *(const lua_Integer *)p);
  } else if (strcmp(type, "lua_Number") == 0) {
    lua_pushnumber(L, *(const lua_Number *)p);
  } else if (strcmp(type, "pointer") == 0) {
    lua_pushpointer(L, *(void **)p);
  } else if (strcmp(type, "string") == 0) {
    lua_pushstring(L, *(const char **)p);
  } else {
    luaL_error(L, "unsupported type for pointer read: %s", type);
  }
}

static void ptr_write_value (lua_State *L, void *p, const char *type, int idx) {
  if (strcmp(type, "int") == 0) {
    *(int *)p = (int)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "float") == 0) {
    *(float *)p = (float)luaL_checknumber(L, idx);
  } else if (strcmp(type, "double") == 0) {
    *(double *)p = (double)luaL_checknumber(L, idx);
  } else if (strcmp(type, "char") == 0) {
    *(char *)p = (char)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "unsigned char") == 0 || strcmp(type, "byte") == 0) {
    *(unsigned char *)p = (unsigned char)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "unsigned int") == 0) {
    *(unsigned int *)p = (unsigned int)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "short") == 0) {
    *(short *)p = (short)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "unsigned short") == 0) {
    *(unsigned short *)p = (unsigned short)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "long") == 0) {
    *(long *)p = (long)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "unsigned long") == 0) {
    *(unsigned long *)p = (unsigned long)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "size_t") == 0) {
    *(size_t *)p = (size_t)luaL_checkinteger(L, idx);
  } else if (strcmp(type, "lua_Integer") == 0) {
    *(lua_Integer *)p = luaL_checkinteger(L, idx);
  } else if (strcmp(type, "lua_Number") == 0) {
    *(lua_Number *)p = luaL_checknumber(L, idx);
  } else if (strcmp(type, "pointer") == 0) {
    *(void **)p = (void *)lua_topointer(L, idx);
  } else {
    luaL_error(L, "unsupported type for pointer write: %s", type);
  }
}


static int l_ptr_read (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  const char *type = luaL_checkstring(L, 2);
  ptr_read_value(L, p, type);
  return 1;
}

static int l_ptr_get (lua_State *L) {
  const char *p = (const char *)lua_topointer(L, 1);
  ptrdiff_t offset = luaL_checkinteger(L, 2);
  const char *type = luaL_checkstring(L, 3);
  ptr_read_value(L, p + offset, type);
  return 1;
}


static int l_ptr_write (lua_State *L) {
  void *p = (void *)lua_topointer(L, 1);
  const char *type = luaL_checkstring(L, 2);
  ptr_write_value(L, p, type, 3);
  return 0;
}

static int l_ptr_set (lua_State *L) {
  char *p = (char *)lua_topointer(L, 1);
  ptrdiff_t offset = luaL_checkinteger(L, 2);
  const char *type = luaL_checkstring(L, 3);
  ptr_write_value(L, p + offset, type, 4);
  return 0;
}


static int l_ptr_malloc (lua_State *L) {
  size_t size = luaL_checkinteger(L, 1);
  void *p = malloc(size);
  if (p == NULL) return luaL_error(L, "malloc failed");
  lua_pushpointer(L, p);
  return 1;
}


static int l_ptr_free (lua_State *L) {
  /* Manual memory management for pointers */
  if (lua_ispointer(L, 1)) {
    void *p = (void *)lua_topointer(L, 1);
    free(p);
  }
  return 0;
}

static int l_ptr_string (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  if (lua_gettop(L) >= 2) {
    size_t len = luaL_checkinteger(L, 2);
    lua_pushlstring(L, (const char *)p, len);
  } else {
    lua_pushstring(L, (const char *)p);
  }
  return 1;
}

static int l_ptr_copy (lua_State *L) {
  void *dst = (void *)lua_topointer(L, 1);
  const void *src = lua_topointer(L, 2);
  size_t len = luaL_checkinteger(L, 3);
  memcpy(dst, src, len);
  return 0;
}

static int l_ptr_move (lua_State *L) {
  void *dst = (void *)lua_topointer(L, 1);
  const void *src = lua_topointer(L, 2);
  size_t len = luaL_checkinteger(L, 3);
  memmove(dst, src, len);
  return 0;
}

static int l_ptr_fill (lua_State *L) {
  void *p = (void *)lua_topointer(L, 1);
  int val = luaL_checkinteger(L, 2);
  size_t len = luaL_checkinteger(L, 3);
  memset(p, val, len);
  return 0;
}

static int l_ptr_compare (lua_State *L) {
  const void *p1 = lua_topointer(L, 1);
  const void *p2 = lua_topointer(L, 2);
  size_t len = luaL_checkinteger(L, 3);
  lua_pushinteger(L, memcmp(p1, p2, len));
  return 1;
}

static int l_ptr_of (lua_State *L) {
  size_t len;
  const char *s = luaL_checklstring(L, 1, &len);
  lua_pushpointer(L, (void *)s);
  return 1;
}

static int l_ptr_null (lua_State *L) {
  lua_pushpointer(L, NULL);
  return 1;
}

static int l_ptr_is_null (lua_State *L) {
  const void *p = lua_topointer(L, 1);
  lua_pushboolean(L, p == NULL);
  return 1;
}

static int l_ptr_equal (lua_State *L) {
  const void *p1 = lua_topointer(L, 1);
  const void *p2 = lua_topointer(L, 2);
  lua_pushboolean(L, p1 == p2);
  return 1;
}

static int l_ptr_tohex (lua_State *L) {
  const unsigned char *p = (const unsigned char *)lua_topointer(L, 1);
  size_t len = luaL_checkinteger(L, 2);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (size_t i = 0; i < len; i++) {
    char buff[4];
    sprintf(buff, "%02X", p[i]);
    luaL_addstring(&b, buff);
    if (i < len - 1) luaL_addchar(&b, ' ');
  }
  luaL_pushresult(&b);
  return 1;
}

/*
** Register the ptr module functions
*/
static const luaL_Reg ptrlib[] = {
  {"addr", l_ptr_addr},
  {"add", l_ptr_add},
  {"inc", l_ptr_inc},
  {"dec", l_ptr_dec},
  {"sub", l_ptr_sub},
  {"read", l_ptr_read},
  {"write", l_ptr_write},
  {"get", l_ptr_get},
  {"set", l_ptr_set},
  {"malloc", l_ptr_malloc},
  {"free", l_ptr_free},
  {"string", l_ptr_string},
  {"copy", l_ptr_copy},
  {"move", l_ptr_move},
  {"fill", l_ptr_fill},
  {"compare", l_ptr_compare},
  {"of", l_ptr_of},
  {"null", l_ptr_null},
  {"is_null", l_ptr_is_null},
  {"equal", l_ptr_equal},
  {"tohex", l_ptr_tohex},
  {NULL, NULL}
};


/*
** Open the ptr module
*/
LUAMOD_API int luaopen_ptr (lua_State *L) {
  luaL_newlib(L, ptrlib);

  /* Create metatable for pointers */
  lua_pushpointer(L, NULL); /* push a dummy pointer */
  lua_createtable(L, 0, 0); /* create metatable */

  /* Set __index to the ptr library */
  lua_pushvalue(L, -3); /* ptr library */
  lua_setfield(L, -2, "__index");

  /* Set metatable for pointer type */
  lua_setmetatable(L, -2);
  lua_pop(L, 1); /* pop dummy pointer */

  return 1;
}
