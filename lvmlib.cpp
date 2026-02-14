/*
** vm library for Lua
** See Copyright Notice in lua.h
*/

#define lvmlib_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lvm.h"
#include "lstate.h"
#include "lobject.h"
#include "ldo.h"


static int vm_execute (lua_State *L) {
  /* 直接调用VM执行当前栈顶的函数 */
  luaL_checktype(L, 1, LUA_TFUNCTION);
  int nargs = lua_gettop(L) - 1;
  int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
  if (status != LUA_OK) {
    /* 执行失败，错误信息已经在栈顶 */
    return 1;
  }
  return lua_gettop(L);
}


static int vm_concat (lua_State *L) {
  /* 调用VM的concat函数 */
  int n = lua_gettop(L);
  if (n == 0) {
    lua_pushliteral(L, "");
    return 1;
  }
  luaV_concat(L, n);
  return 1;
}


static int vm_objlen (lua_State *L) {
  /* 获取对象长度，使用标准Lua API */
  lua_len(L, 1);
  return 1;
}


static int vm_equal (lua_State *L) {
  /* 比较两个值是否相等，使用标准Lua API */
  int res = lua_compare(L, 1, 2, LUA_OPEQ);
  lua_pushboolean(L, res);
  return 1;
}


static int vm_lessthan (lua_State *L) {
  /* 比较两个值的大小，使用标准Lua API */
  int res = lua_compare(L, 1, 2, LUA_OPLT);
  lua_pushboolean(L, res);
  return 1;
}


static int vm_lessequal (lua_State *L) {
  /* 比较两个值的大小，使用标准Lua API */
  int res = lua_compare(L, 1, 2, LUA_OPLE);
  lua_pushboolean(L, res);
  return 1;
}


static int vm_tonumber (lua_State *L) {
  /* 将值转换为数字，使用标准Lua API */
  lua_Number n;
  if (lua_tonumberx(L, 1, NULL)) {
    lua_pushvalue(L, -1);
    return 1;
  } else {
    lua_pushnil(L);
    return 1;
  }
}


static int vm_tointeger (lua_State *L) {
  /* 将值转换为整数，使用标准Lua API */
  lua_Integer i;
  if (lua_tointegerx(L, 1, NULL)) {
    lua_pushvalue(L, -1);
    return 1;
  } else {
    lua_pushnil(L);
    return 1;
  }
}


static int vm_gcinfo (lua_State *L) {
  /* 获取GC信息，使用标准Lua API */
  lua_pushinteger(L, lua_gc(L, LUA_GCCOUNT, 0) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0));
  return 1;
}


static int vm_gettop (lua_State *L) {
  /* 获取栈顶位置 */
  lua_pushinteger(L, lua_gettop(L));
  return 1;
}


static int vm_memory (lua_State *L) {
  /* 获取内存使用情况，使用标准Lua API */
  lua_pushinteger(L, lua_gc(L, LUA_GCCOUNT, 0) * 1024 + lua_gc(L, LUA_GCCOUNTB, 0));
  lua_pushinteger(L, lua_gc(L, LUA_GCCOUNT, 0));
  return 2;
}


static int vm_gcstep (lua_State *L) {
  /* 执行一次GC步骤 */
  int step = luaL_optinteger(L, 1, 0);
  int res = lua_gc(L, LUA_GCSTEP, step);
  lua_pushboolean(L, res);
  return 1;
}


static int vm_gccollect (lua_State *L) {
  /* 执行完整GC */
  lua_gc(L, LUA_GCCOLLECT);
  return 0;
}


static int vm_newthread (lua_State *L) {
  /* 创建新的线程（VM实例） */
  lua_State *newL = lua_newthread(L);
  return 1;
}


static int vm_status (lua_State *L) {
  /* 获取线程状态 */
  luaL_checktype(L, 1, LUA_TTHREAD);
  lua_State *thread = lua_tothread(L, 1);
  int status = lua_status(thread);
  const char *statestr;
  switch (status) {
    case LUA_OK: statestr = "ok";
      break;
    case LUA_YIELD: statestr = "yield";
      break;
    case LUA_ERRRUN: statestr = "runtime error";
      break;
    case LUA_ERRSYNTAX: statestr = "syntax error";
      break;
    case LUA_ERRMEM: statestr = "memory error";
      break;
    case LUA_ERRERR: statestr = "error handler error";
      break;
    default: statestr = "unknown";
      break;
  }
  lua_pushstring(L, statestr);
  return 1;
}


static int vm_resume (lua_State *L) {
  /* 恢复线程执行 */
  luaL_checktype(L, 1, LUA_TTHREAD);
  lua_State *thread = lua_tothread(L, 1);
  int nargs = lua_gettop(L) - 1;
  int nres;
  int status = lua_resume(thread, L, nargs, &nres);
  if (status == LUA_OK || status == LUA_YIELD) {
    if (status == LUA_YIELD) {
      lua_pushboolean(L, 1);  /* 表示yield */
      return nres + 1;
    } else {
      return nres;
    }
  } else {
    /* 错误 */
    lua_pushboolean(L, 0);
    lua_pushstring(L, lua_tostring(thread, -1));
    lua_pop(thread, 1);
    return 2;
  }
}


static int vm_yield (lua_State *L) {
  /* 挂起当前线程 */
  int nargs = lua_gettop(L);
  return lua_yield(L, nargs);
}


static int vm_currentthread (lua_State *L) {
  /* 获取当前线程 */
  lua_pushthread(L);
  return 1;
}


static int vm_typename (lua_State *L) {
  /* 获取值的类型名称 */
  int t = lua_type(L, 1);
  lua_pushstring(L, lua_typename(L, t));
  return 1;
}


static int vm_getci (lua_State *L) {
  /* 获取当前CallInfo结构信息 */
  CallInfo *ci = L->ci;
  lua_newtable(L);
  lua_pushinteger(L, ci->nresults);
  lua_setfield(L, -2, "nresults");
  lua_pushboolean(L, isLua(ci));
  lua_setfield(L, -2, "isLua");
  lua_pushboolean(L, isLuacode(ci));
  lua_setfield(L, -2, "isLuacode");
  return 1;
}


static int vm_getstack (lua_State *L) {
  /* 获取栈信息 */
  int i;
  int n = lua_gettop(L);
  lua_newtable(L);
  for (i = 1; i <= n; i++) {
    lua_pushvalue(L, i);
    lua_seti(L, -2, i);
  }
  return 1;
}


static int vm_gcstop (lua_State *L) {
  /* 停止GC */
  lua_gc(L, LUA_GCSTOP, 0);
  return 0;
}


static int vm_gcstart (lua_State *L) {
  /* 启动GC */
  lua_gc(L, LUA_GCRESTART, 0);
  return 0;
}


static int vm_gcsetpause (lua_State *L) {
  /* 设置GC暂停时间 */
  int pause = luaL_checkinteger(L, 1);
  lua_pushinteger(L, lua_gc(L, LUA_GCSETPAUSE, pause));
  return 1;
}


static int vm_gcsetstepmul (lua_State *L) {
  /* 设置GC步进倍数 */
  int stepmul = luaL_checkinteger(L, 1);
  lua_pushinteger(L, lua_gc(L, LUA_GCSETSTEPMUL, stepmul));
  return 1;
}


static int vm_gcinc (lua_State *L) {
  /* 增量GC */
  int bytes = luaL_optinteger(L, 1, 0);
  lua_pushinteger(L, lua_gc(L, LUA_GCINC, bytes));
  return 1;
}


static int vm_getregistry (lua_State *L) {
  /* 获取注册表 */
  lua_pushvalue(L, LUA_REGISTRYINDEX);
  return 1;
}


static int vm_getglobalenv (lua_State *L) {
  /* 获取全局环境 */
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  return 1;
}


static int vm_setglobalenv (lua_State *L) {
  /* 设置全局环境 */
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  return 0;
}


static int vm_isfunction (lua_State *L) {
  /* 检查是否为函数 */
  lua_pushboolean(L, lua_isfunction(L, 1));
  return 1;
}


static int vm_isnil (lua_State *L) {
  /* 检查是否为nil */
  lua_pushboolean(L, lua_isnil(L, 1));
  return 1;
}


static int vm_isboolean (lua_State *L) {
  /* 检查是否为boolean */
  lua_pushboolean(L, lua_isboolean(L, 1));
  return 1;
}


static int vm_isnumber (lua_State *L) {
  /* 检查是否为number */
  lua_pushboolean(L, lua_isnumber(L, 1));
  return 1;
}


static int vm_isstring (lua_State *L) {
  /* 检查是否为string */
  lua_pushboolean(L, lua_isstring(L, 1));
  return 1;
}


static int vm_istable (lua_State *L) {
  /* 检查是否为table */
  lua_pushboolean(L, lua_istable(L, 1));
  return 1;
}


static int vm_isuserdata (lua_State *L) {
  /* 检查是否为userdata */
  lua_pushboolean(L, lua_isuserdata(L, 1));
  return 1;
}


static int vm_isthread (lua_State *L) {
  /* 检查是否为thread */
  lua_pushboolean(L, lua_isthread(L, 1));
  return 1;
}


static int vm_iscfunction (lua_State *L) {
  /* 检查是否为C函数 */
  lua_pushboolean(L, lua_iscfunction(L, 1));
  return 1;
}


static int vm_rawget (lua_State *L) {
  /* 原始获取table值 */
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_rawget(L, 1);
  return 1;
}


static int vm_rawset (lua_State *L) {
  /* 原始设置table值 */
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_rawset(L, 1);
  return 0;
}


static int vm_rawlen (lua_State *L) {
  /* 原始获取长度 */
  lua_pushinteger(L, lua_rawlen(L, 1));
  return 1;
}


static int vm_createtable (lua_State *L) {
  /* 创建table */
  int narr = luaL_optinteger(L, 1, 0);
  int nrec = luaL_optinteger(L, 2, 0);
  lua_createtable(L, narr, nrec);
  return 1;
}


static int vm_newuserdata (lua_State *L) {
  /* 创建userdata */
  size_t size = luaL_checkinteger(L, 1);
  lua_newuserdata(L, size);
  return 1;
}


static int vm_getmetatable (lua_State *L) {
  /* 获取元表 */
  if (lua_getmetatable(L, 1)) {
    return 1;
  } else {
    return 0;
  }
}


static int vm_setmetatable (lua_State *L) {
  /* 设置元表 */
  lua_setmetatable(L, 1);
  return 0;
}


static int vm_error (lua_State *L) {
  /* 抛出错误 */
  const char *msg = luaL_checkstring(L, 1);
  luaL_error(L, "%s", msg);
  return 0;
}


static int vm_assert (lua_State *L) {
  /* 断言 */
  if (!lua_toboolean(L, 1)) {
    const char *msg = luaL_optstring(L, 2, "assertion failed!");
    luaL_error(L, "%s", msg);
  }
  return lua_gettop(L);
}


static int vm_traceback (lua_State *L) {
  /* 获取回溯信息 */
  int level = luaL_optinteger(L, 1, 1);
  luaL_traceback(L, L, NULL, level);
  return 1;
}


static const luaL_Reg vm_funcs[] = {
  {"execute", vm_execute},
  {"concat", vm_concat},
  {"objlen", vm_objlen},
  {"equal", vm_equal},
  {"lt", vm_lessthan},
  {"le", vm_lessequal},
  {"tonumber", vm_tonumber},
  {"tointeger", vm_tointeger},
  {"gcinfo", vm_gcinfo},
  {"gettop", vm_gettop},
  {"memory", vm_memory},
  {"gcstep", vm_gcstep},
  {"gccollect", vm_gccollect},
  {"newthread", vm_newthread},
  {"status", vm_status},
  {"resume", vm_resume},
  {"yield", vm_yield},
  {"currentthread", vm_currentthread},
  {"typename", vm_typename},
  {"getci", vm_getci},
  {"getstack", vm_getstack},
  {"gcstop", vm_gcstop},
  {"gcstart", vm_gcstart},
  {"gcsetpause", vm_gcsetpause},
  {"gcsetstepmul", vm_gcsetstepmul},
  {"gcinc", vm_gcinc},
  {"getregistry", vm_getregistry},
  {"getglobalenv", vm_getglobalenv},
  {"setglobalenv", vm_setglobalenv},
  {"isfunction", vm_isfunction},
  {"isnil", vm_isnil},
  {"isboolean", vm_isboolean},
  {"isnumber", vm_isnumber},
  {"isstring", vm_isstring},
  {"istable", vm_istable},
  {"isuserdata", vm_isuserdata},
  {"isthread", vm_isthread},
  {"iscfunction", vm_iscfunction},
  {"rawget", vm_rawget},
  {"rawset", vm_rawset},
  {"rawlen", vm_rawlen},
  {"createtable", vm_createtable},
  {"newuserdata", vm_newuserdata},
  {"getmetatable", vm_getmetatable},
  {"setmetatable", vm_setmetatable},
  {"error", vm_error},
  {"assert", vm_assert},
  {"traceback", vm_traceback},
  {NULL, NULL}
};


LUAMOD_API int luaopen_vm (lua_State *L) {
  luaL_newlib(L, vm_funcs);
  return 1;
}
