/*
** $Id: linit.c $
** Initialization of libraries for lua.c and other clients
** See Copyright Notice in lua.h
*/


#define linit_c
#define LUA_LIB

/*
** If you embed Lua in your program and need to open the standard
** libraries, call luaL_openlibs in your program. If you need a
** different set of libraries, copy this file to your project and edit
** it to suit your needs.
**
** You can also *preload* libraries, so that a later 'require' can
** open the library, which is already linked to the application.
** For that, do the following code:
**
**  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
**  lua_pushcfunction(L, luaopen_modname);
**  lua_setfield(L, -2, modname);
**  lua_pop(L, 1);  // remove PRELOAD table
*/

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "lualib.h"
#include "lauxlib.h"
#include "ltranslator.h"

/* 声明libc库的初始化函数 */
int luaopen_libc(lua_State *L);

/* 声明logtable库的初始化函数 */
int luaopen_logtable(lua_State *L);

/* 声明thread库的初始化函数 */
int luaopen_thread(lua_State *L);

// clang and ffi libraries

/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
*/
/*
** Standard Libraries. (Must be listed in the same ORDER of their
** respective constants LUA_<libname>K.)
** Note: Custom libraries are added after standard libraries with consecutive masks.
*/
static const luaL_Reg stdlibs[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_LOADLIBNAME, luaopen_package},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_DBLIBNAME, luaopen_debug},
  {LUA_IOLIBNAME, luaopen_io},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_UTF8LIBNAME, luaopen_utf8},
  {LUA_BOOLIBNAME, luaopen_bool},
  {LUA_UDATALIBNAME, luaopen_userdata},
  {LUA_VMLIBNAME, luaopen_vm},
  {LUA_BITLIBNAME, luaopen_bit},
  {LUA_PTRLIBNAME, luaopen_ptr},
  {"bit32", luaopen_bit},
  {"thread", luaopen_thread},

#ifndef _WIN32
  {LUA_SMGRNAME, luaopen_smgr},
  {"translator", luaopen_translator},
  {"logtable", luaopen_logtable},

  // 仅安卓额外加 libc
#ifdef __ANDROID__
  {"libc", luaopen_libc},
#endif

#endif

  {NULL, NULL}
};


/*
** require and preload selected standard libraries
*/
LUALIB_API void luaL_openselectedlibs (lua_State *L, int load, int preload) {
  int mask;
  const luaL_Reg *lib;
  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  for (lib = stdlibs, mask = 1; lib->name != NULL; lib++, mask <<= 1) {
    if (load & mask) {  /* selected? */
      luaL_requiref(L, lib->name, lib->func, 1);  /* require library */
      lua_pop(L, 1);  /* remove result from the stack */
    }
    else if (preload & mask) {  /* selected? */
      lua_pushcfunction(L, lib->func);
      lua_setfield(L, -2, lib->name);  /* add library to PRELOAD table */
    }
  }
  lua_pop(L, 1);  /* remove PRELOAD table */
}


/*
** All standard libraries
*/
static const luaL_Reg loadedlibs[] = {
  {LUA_GNAME, luaopen_base},
  {LUA_LOADLIBNAME, luaopen_package},
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
  {"bit32", luaopen_bit},
  {"thread", luaopen_thread},

#ifndef _WIN32
  {LUA_SMGRNAME, luaopen_smgr},
  {"translator", luaopen_translator},
  {"logtable", luaopen_logtable},

  // 仅安卓额外加载 libc
#ifdef __ANDROID__
  {"libc", luaopen_libc},
#endif

#endif

  {NULL, NULL}
};



LUALIB_API void luaL_openlibs (lua_State *L) {
  const luaL_Reg *lib;
  /* "require" functions from 'loadedlibs' and set results to global table */
  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);  /* remove lib */
  }
}

