/*
** userdata library for Lua
** See Copyright Notice in lua.h
*/

#define ludatalib_c
#define LUA_LIB

#include "lprefix.h"

#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"


static int userdata_is_userdata (lua_State *L) {
  lua_pushboolean(L, lua_type(L, 1) == LUA_TUSERDATA);
  return 1;
}


static int userdata_is_light (lua_State *L) {
  lua_pushboolean(L, lua_islightuserdata(L, 1));
  return 1;
}


static int userdata_type (lua_State *L) {
  if (lua_islightuserdata(L, 1))
    lua_pushstring(L, "light");
  else if (lua_type(L, 1) == LUA_TUSERDATA)
    lua_pushstring(L, "full");
  else
    luaL_pushfail(L);
  return 1;
}


static int userdata_equals (lua_State *L) {
  int type1 = lua_type(L, 1);
  int type2 = lua_type(L, 2);
  if (type1 != LUA_TUSERDATA || type2 != LUA_TUSERDATA) {
    lua_pushboolean(L, 0);
    return 1;
  }
  if (lua_islightuserdata(L, 1) && lua_islightuserdata(L, 2)) {
    void *p1 = lua_touserdata(L, 1);
    void *p2 = lua_touserdata(L, 2);
    lua_pushboolean(L, p1 == p2);
  } else {
    /* full userdata uses metamethod __eq */
    int res = lua_compare(L, 1, 2, LUA_OPEQ);
    lua_pushboolean(L, res);
  }
  return 1;
}


static int userdata_tostring (lua_State *L) {
  if (lua_type(L, 1) != LUA_TUSERDATA) {
    luaL_pushfail(L);
    return 1;
  }
  lua_tostring(L, 1);
  return 1;
}


static int userdata_address (lua_State *L) {
  if (!lua_islightuserdata(L, 1)) {
    luaL_pushfail(L);
    return 1;
  }
  void *p = lua_touserdata(L, 1);
  char buff[32];
  sprintf(buff, "%p", p);
  lua_pushstring(L, buff);
  return 1;
}


static int userdata_fromany (lua_State *L) {
  int type = lua_type(L, 1);
  
  switch (type) {
    case LUA_TNIL:
    case LUA_TBOOLEAN:
    case LUA_TNUMBER:
    case LUA_TSTRING:
      {
        /* Create a full userdata and attach the value as a user value */
        void *udata = lua_newuserdata(L, 0);
        lua_pushvalue(L, 1);
        lua_setuservalue(L, -2);
        break;
      }
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TTHREAD:
      {
        /* Create a light userdata pointing to the object's address */
        const void *addr = lua_topointer(L, 1);
        lua_pushlightuserdata(L, (void *)addr);
        break;
      }
    case LUA_TUSERDATA:
      {
        /* Already a userdata, just return it */
        lua_pushvalue(L, 1);
        break;
      }
    default:
      {
        /* Unknown type, return nil */
        lua_pushnil(L);
        break;
      }
  }
  
  return 1;
}


static const luaL_Reg userdata_funcs[] = {
  {"isuserdata", userdata_is_userdata},
  {"islight", userdata_is_light},
  {"type", userdata_type},
  {"equals", userdata_equals},
  {"tostring", userdata_tostring},
  {"address", userdata_address},
  {"fromany", userdata_fromany},
  {NULL, NULL}
};


LUAMOD_API int luaopen_userdata (lua_State *L) {
  luaL_newlib(L, userdata_funcs);
  return 1;
}
