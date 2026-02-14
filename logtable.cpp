/*
** logtable.c
** Table access logging module
** Provides table access interception and filtering functionality
*/

#define logtable_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "ltable.h"

#if defined(__ANDROID__) && defined(ANDROID_NDK)
#include <android/log.h>
#define LOG_TAG "lua"
#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

static int logtable_onlog(lua_State *L) {
  int enable = lua_toboolean(L, 1);
  int result = luaH_enable_access_log(L, enable);
  lua_pushboolean(L, result);
  return 1;
}

static int logtable_getlogpath(lua_State *L) {
  const char *path = luaH_get_log_path(L);
  if (path && path[0] != '\0') {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

static int logtable_setfilter(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_access_filter_enabled(enabled);
  return 0;
}

static int logtable_clearfilter(lua_State *L) {
  (void)L;
  luaH_clear_access_filters();
  return 0;
}

static int logtable_addinkey(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_key_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_exckey(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_key_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_addinval(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_value_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_exczval(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_value_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_addinop(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_include_op_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_exczop(lua_State *L) {
  const char *pattern = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_op_filter(pattern);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_keyrange(lua_State *L) {
  lua_Integer min_val = luaL_optinteger(L, 1, 0);
  lua_Integer max_val = luaL_optinteger(L, 2, 0);
  luaH_set_key_int_range((int)min_val, (int)max_val);
  return 0;
}

static int logtable_valrange(lua_State *L) {
  lua_Integer min_val = luaL_optinteger(L, 1, 0);
  lua_Integer max_val = luaL_optinteger(L, 2, 0);
  luaH_set_value_int_range((int)min_val, (int)max_val);
  return 0;
}

static int logtable_setdedup(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_dedup_enabled(enabled);
  return 0;
}

static int logtable_setunique(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_show_unique_only(enabled);
  return 0;
}

static int logtable_resetdedup(lua_State *L) {
  (void)L;
  luaH_reset_dedup_cache();
  return 0;
}

static int logtable_addinkeytype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_include_key_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_exckeytype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_key_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_addinvaltype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_include_value_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_exczvaltype(lua_State *L) {
  const char *type = luaL_checkstring(L, 1);
  int count = luaH_add_exclude_value_type_filter(type);
  lua_pushinteger(L, count);
  return 1;
}

static int logtable_setintelligent(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_intelligent_mode(enabled);
  return 0;
}

static int logtable_getintelligent(lua_State *L) {
  int enabled = luaH_is_intelligent_mode_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

static int logtable_setjnienv(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_filter_jnienv(enabled);
  return 0;
}

static int logtable_getjnienv(lua_State *L) {
  int enabled = luaH_is_filter_jnienv_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

static int logtable_setuserdata(lua_State *L) {
  int enabled = lua_toboolean(L, 1);
  luaH_set_filter_userdata(enabled);
  return 0;
}

static int logtable_getuserdata(lua_State *L) {
  int enabled = luaH_is_filter_userdata_enabled();
  lua_pushboolean(L, enabled);
  return 1;
}

static const luaL_Reg logtable_funcs[] = {
  {"onlog", logtable_onlog},
  {"getlogpath", logtable_getlogpath},
  {"setfilter", logtable_setfilter},
  {"clearfilter", logtable_clearfilter},
  {"addinkey", logtable_addinkey},
  {"exckey", logtable_exckey},
  {"addinval", logtable_addinval},
  {"exczval", logtable_exczval},
  {"addinop", logtable_addinop},
  {"exczop", logtable_exczop},
  {"keyrange", logtable_keyrange},
  {"valrange", logtable_valrange},
  {"setdedup", logtable_setdedup},
  {"setunique", logtable_setunique},
  {"resetdedup", logtable_resetdedup},
  {"addinkeytype", logtable_addinkeytype},
  {"exckeytype", logtable_exckeytype},
  {"addinvaltype", logtable_addinvaltype},
  {"exczvaltype", logtable_exczvaltype},
  {"setintelligent", logtable_setintelligent},
  {"getintelligent", logtable_getintelligent},
  {"setjnienv", logtable_setjnienv},
  {"getjnienv", logtable_getjnienv},
  {"setuserdata", logtable_setuserdata},
  {"getuserdata", logtable_getuserdata},
  {NULL, NULL}
};

LUAMOD_API int luaopen_logtable(lua_State *L) {
  luaL_newlib(L, logtable_funcs);
  return 1;
}
